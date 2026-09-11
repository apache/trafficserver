# Connection-Error Handling and Client Keep-Alive Shutdown

This note traces the Traffic Server (TS) code path from a failed parent/origin connection through to the moment TS forces the downstream client connection into non-keepalive, and describes how the client reacts. Each section includes the exact code that participates in the decision.

## 1. Detecting the Connect Failure

`HttpSM::state_http_server_open` drives the non-blocking connect. Any connect timeout or socket error drops into the `VC_EVENT_*` branch below, which stamps the state and records the errno before closing the half-open `NetVConnection`:

```c++
int
HttpSM::state_http_server_open(int event, void *data)
{
  ...
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    t_state.set_connect_fail(ETIMEDOUT);
  /* fallthrough */
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
  case NET_EVENT_OPEN_FAILED: {
    t_state.current.state = HttpTransact::CONNECTION_ERROR;
    ...
    if (_netvc != nullptr) {
      if (event == VC_EVENT_ERROR || event == NET_EVENT_OPEN_FAILED) {
        t_state.set_connect_fail(_netvc->lerrno);
      }
      _netvc->do_io_close();
      _netvc = nullptr;
    }
    ...
  }
  ...
}
```

If the error happens later—while writing the request header or waiting for the response—`HttpSM::handle_server_setup_error` applies the same bookkeeping (`t_state.current.state = CONNECTION_ERROR`, `set_connect_fail`, close VC) before handing control back to Traffic Cop.

## 2. Transact Logs the Failure

`HttpTransact::handle_response_from_server` looks at `t_state.current.state`. When it sees `CONNECTION_ERROR` it logs the failure and either retries or bails:

```c++
void
HttpTransact::handle_response_from_server(State *s)
{
  ...
  case CONNECTION_ERROR:
    ...
    if (is_request_retryable(s) && s->current.retry_attempts.get() < max_connect_retries &&
        !HttpTransact::is_response_valid(s, &s->hdr_info.server_response)) {
      ...
      retry_server_connection_not_open(s, s->current.state, max_connect_retries);
      ...
    } else {
      error_log_connection_failure(s, s->current.state);
      TxnDbg(..., "Error. No more retries.");
      SET_VIA_STRING(VIA_DETAIL_SERVER_CONNECT, VIA_DETAIL_SERVER_FAILURE);
      handle_server_connection_not_open(s);
    }
    break;
  ...
}
```

Every retry first forces the failed server session out of the keep-alive pool:

```c++
void
HttpTransact::retry_server_connection_not_open(State *s, ServerState_t conn_state, unsigned max_retries)
{
  ...
  s->current.server->keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
  s->current.retry_attempts.increment();
}
```

## 3. Forcing the User-Agent Connection to Close

When the retry budget is exhausted, `handle_server_connection_not_open` routes to `handle_parent_down` / `handle_server_down`, which build an internal error. `HttpTransact::build_error_response` deliberately disables user-agent keep-alive before those headers are emitted:

```c++
void
HttpTransact::build_error_response(State *s, HTTPStatus status_code, const char *reason, const char *body_type)
{
  ...
  if (status_code == HTTPStatus::REQUEST_TIMEOUT || s->hdr_info.client_request.get_content_length() != 0 ||
      s->client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED) {
    s->client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
  }
  ...
  if ((s->state_machine->get_ua_txn() && s->state_machine->get_ua_txn()->is_outbound_transparent()) &&
      (status_code == HTTPStatus::INTERNAL_SERVER_ERROR || status_code == HTTPStatus::GATEWAY_TIMEOUT ||
       status_code == HTTPStatus::BAD_GATEWAY || status_code == HTTPStatus::SERVICE_UNAVAILABLE)) {
    s->client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
  }
  ...
}
```

`handle_response_keep_alive_headers` then honors that flag by inserting `Connection: close` (or `Proxy-Connection: close`) into the proxy response and calling `set_close_connection` on the client session:

```c++
void
HttpTransact::handle_response_keep_alive_headers(State *s, HTTPVersion ver, HTTPHdr *heads)
{
  ...
  if (s->client_info.keep_alive != HTTPKeepAlive::KEEPALIVE) {
    ka_action = KA_Action_t::DISABLED;
  }
  ...
  case KA_Action_t::CLOSE:
  case KA_Action_t::DISABLED:
    if (s->client_info.keep_alive != HTTPKeepAlive::NO_KEEPALIVE || (ver == HTTP_1_1)) {
      if (s->client_info.proxy_connect_hdr) {
        heads->value_set(..., "close"sv);
      } else if (s->state_machine->get_ua_txn() != nullptr) {
        s->state_machine->get_ua_txn()->set_close_connection(*heads);
      }
      s->client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
    }
    break;
  ...
}
```

## 4. Client-Side Behavior

On the downstream (first-layer) side, `HttpSM::tunnel_handler_ua` trusts the `keep_alive` flag. Because the earlier logic set it to `NO_KEEPALIVE`, the write-complete handler leaves `close_connection = true`, which causes the user-agent session to terminate instead of re-entering the keep-alive pool:

```c++
int
HttpSM::tunnel_handler_ua(int event, HttpTunnelConsumer *c)
{
  ...
  case VC_EVENT_WRITE_COMPLETE:
    c->write_success          = true;
    t_state.client_info.abort = HttpTransact::DIDNOT_ABORT;
    if (t_state.client_info.keep_alive == HTTPKeepAlive::KEEPALIVE) {
      ...
      close_connection = false;
    }
    break;
  ...
}
```

Earlier, `HttpSM::do_drain_request_body` (and `set_close_connection`) already updated the downstream headers, so the first-layer ATS receives an explicit `Connection: close` and, per RFC 7230 and `Http1ClientSession::set_close_connection`, immediately closes its side instead of tracking the socket for reuse.

## Summary

1. Connect failure → `CONNECTION_ERROR` state (`HttpSM` sets it when the connect VC fails).
2. `HttpTransact` logs `CONNECT: … CONNECTION_ERROR`, optionally retries, and forces the failed server session out of the pool.
3. The generated error response clears `client_info.keep_alive`, inserts `Connection: close`, and calls `set_close_connection`.
4. The downstream client (the first-layer ATS) sees the header, closes the socket, and drops it from its keep-alive pool—ensuring sockets used for failed multi-layer connects can’t be reused.
