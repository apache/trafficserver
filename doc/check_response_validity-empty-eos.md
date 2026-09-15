# Empty origin response with EOS

This note walks the code path for the case where the origin server sends no response bytes at all and then closes the connection with EOS.

## Short answer

- In the real transaction path, `check_response_validity()` is not called.
- The transaction reaches `HandleResponse()` with `current.state == CONNECTION_CLOSED`, so `is_response_valid()` returns `false` early and sets `response_error = CONNECTION_OPEN_FAILED`.
- If `check_response_validity()` were called directly on the untouched `server_response` header object anyway, it would return `ResponseError_t::MISSING_STATUS_CODE`.

## Code path

1. `HttpSM::setup_server_read_response_header()` resets the origin response header with:
   - `t_state.hdr_info.server_response.destroy();`
   - `t_state.hdr_info.server_response.create(HTTPType::RESPONSE);`

   That creates a valid `HTTPHdr` object with response polarity, but its fields are still zero-initialized. In particular, the response status is still `HTTPStatus::NONE`.

2. `HttpSM::state_read_server_response_header()` receives `VC_EVENT_EOS` before any response bytes are read.
   - It sets `server_entry->eos = true`.
   - `server_response_hdr_bytes` is still `0`.
   - It then calls `t_state.hdr_info.server_response.parse_resp(..., eof = true)`.

3. `HTTPHdr::parse_resp(HTTPParser *, IOBufferReader *, int *, bool)` handles the empty-buffer EOF case specially:
   - if `b_avail <= 0`
   - and `eof == true`
   - and `start == nullptr`
   - then it returns `ParseResult::ERROR` immediately.

   So an empty origin response plus EOS is treated as a parse failure.

4. Back in `HttpSM::state_read_server_response_header()`, this lands in the `ParseResult::ERROR` branch.
   - It sets `t_state.current.state = HttpTransact::PARSE_ERROR`.
   - Because the event was `VC_EVENT_EOS`, it calls `handle_server_setup_error(VC_EVENT_EOS, data)`.

5. `HttpSM::handle_server_setup_error()` then converts the state to the connection-close path:
   - `t_state.current.state = HttpTransact::CONNECTION_CLOSED`
   - `t_state.set_connect_fail(EPIPE)`
   - `call_transact_and_set_next_state(HttpTransact::HandleResponse)`

6. `HttpTransact::HandleResponse()` calls:

   `HttpTransact::is_response_valid(s, &s->hdr_info.server_response)`

   But `is_response_valid()` starts with a guard:

   - if `s->current.state != CONNECTION_ALIVE`
   - set `s->hdr_info.response_error = ResponseError_t::CONNECTION_OPEN_FAILED`
   - return `false`

   Because the state is already `CONNECTION_CLOSED`, execution returns here. `check_response_validity()` is never reached.

## What `check_response_validity()` would return if called directly

Even though the live path does not call it, the return value is straightforward from the initialized header state:

1. `incoming_hdr` is not null.
2. `incoming_hdr->type_get() == HTTPType::RESPONSE` because the header was created with `create(HTTPType::RESPONSE)`.
3. `incoming_hdr->status_get() == HTTPStatus::NONE` because no bytes were parsed into the header.
4. `HttpTransact::check_response_validity()` therefore hits:

   `if (incoming_status == HTTPStatus::NONE) { return ResponseError_t::MISSING_STATUS_CODE; }`

So the helper's direct return would be `ResponseError_t::MISSING_STATUS_CODE`.

## Conclusion

For an origin response of "0 bytes, then EOS":

- Actual transaction behavior: `is_response_valid()` returns `false` early with `response_error = CONNECTION_OPEN_FAILED`.
- `check_response_validity()` is not called on the live path.
- Hypothetical direct call to `check_response_validity(&server_response)`: `ResponseError_t::MISSING_STATUS_CODE`.
