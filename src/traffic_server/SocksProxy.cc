/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/*
  This implements SOCKS server. We intercept the http traffic and send it
  through HTTP. Others are tunneled through directly to the socks server.


*/
#include "tscore/ink_platform.h"
#include "P_Net.h"
#include "I_OneWayTunnel.h"
#include "HttpSessionAccept.h"

enum {
  socksproxy_http_connections_stat,
  socksproxy_tunneled_connections_stat,

  socksproxy_stat_count
};
static RecRawStatBlock *socksproxy_stat_block;

#define SOCKSPROXY_INC_STAT(x) RecIncrRawStat(socksproxy_stat_block, mutex->thread_holding, x)

struct SocksProxy;
typedef int (SocksProxy::*SocksProxyHandler)(int event, void *data);

struct SocksProxy : public Continuation {
  using EventHandler = int (SocksProxy::*)(int, void *);

  /* SocksProxy States:
   *
   *
   *                 NET_EVENT_ACCEPT
   *  SOCKS_INIT  ---------------------->  SOCKS_ACCEPT
   *                                            |
   *                                            |
   *             +------------------------------+--------------------+
   *             |                              |                    |
   *             |                              |                    |
   *         (Bad Ver)                     (Socks v5)            (Socks v4)
   *             |                              |                    |
   *             |                              |                    |
   *             |                          AUTH_DONE                |
   *             |                              |                    |
   *             |                              V                    V
   *             |                       (CMD = CONNECT && Port = http_port)
   *             |                                         |
   *             |                                         |
   *             |                      +-------(Yes)------+-------(No)-------------+
   *             |                      |                                           |
   *             |                      |                                           V
   *             |                      |                                 (Type of Target addr)
   *             |                      |                                     |            |
   *             |                      |                                     |            |
   *             |                      |                                  is IPv4      not IPv4
   *             |                      |                                     |            |
   *             |                      |                                     |            |
   *             |                      V                                     V            |
   *             |                  HTTP_REQ                             SERVER_TUNNEL     |
   *             |                      |                                     |            |
   *             |                      |                                (connect_re)      |
   *             |                      |                                     |            |
   *             V                      V               NET_EVENT_OPEN        |            |
   *        SOCKS_ERROR  -------->  ALL_DONE  <-------------------------------+            |
   *             A                                                            |            |
   *             |                                                            |            |
   *             |                                   NET_EVENT_OPEN_FAILED    |            |
   *             +-------------  RESP_TO_CLIENT  <----------------------------+  <---------+
   *
   */
  enum {
    SOCKS_INIT = 1,
    SOCKS_ACCEPT,
    AUTH_DONE,
    SERVER_TUNNEL,
    HTTP_REQ,
    RESP_TO_CLIENT,
    ALL_DONE,
    SOCKS_ERROR,
  };

  ~SocksProxy() override {}

  int acceptEvent(int event, void *data);
  int mainEvent(int event, void *data);

  int state_read_client_request(int event, void *data);
  int state_read_socks4_client_request(int event, void *data);
  int state_read_socks5_client_auth_methods(int event, void *data);
  int state_send_socks5_auth_method(int event, void *data);
  int state_read_socks5_client_request(int event, void *data);
  int state_handing_over_http_request(int event, void *data);
  int state_send_socks_reply(int event, void *data);

  int parse_socks_client_request(unsigned char *p);
  int setupHttpRequest(unsigned char *p);
  int sendResp(bool granted);

  void init(NetVConnection *netVC);
  void free();

private:
  NetVConnection *clientVC = nullptr;
  VIO *clientVIO           = nullptr;

  MIOBuffer *buf         = nullptr;
  IOBufferReader *reader = nullptr;
  Event *timeout         = nullptr;

  SocksAuthHandler auth_handler = nullptr;
  SocksProxyHandler vc_handler  = nullptr;
  Action *pending_action        = nullptr;

  unsigned char version = 0;
  int port              = 0;
  int state             = SOCKS_INIT;
  int recursion         = 0;
};

ClassAllocator<SocksProxy> socksProxyAllocator("socksProxyAllocator");

void
SocksProxy::init(NetVConnection *netVC)
{
  mutex  = new_ProxyMutex();
  buf    = new_MIOBuffer();
  reader = buf->alloc_reader();

  SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());

  SET_HANDLER((EventHandler)&SocksProxy::acceptEvent);

  handleEvent(NET_EVENT_ACCEPT, netVC);
}

void
SocksProxy::free()
{
  if (buf) {
    free_MIOBuffer(buf);
  }

  mutex = nullptr;

  socksProxyAllocator.free(this);
}

int
SocksProxy::acceptEvent(int event, void *data)
{
  ink_assert(event == NET_EVENT_ACCEPT);
  state = SOCKS_ACCEPT;
  Debug("SocksProxy", "Proxy got accept event");

  clientVC = (NetVConnection *)data;
  clientVC->socks_addr.reset();

  buf->reset();

  SET_HANDLER((EventHandler)&SocksProxy::mainEvent);
  vc_handler = &SocksProxy::state_read_client_request;

  timeout   = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->socks_timeout));
  clientVIO = clientVC->do_io_read(this, INT64_MAX, buf);

  return EVENT_DONE;
}

int
SocksProxy::mainEvent(int event, void *data)
{
  int ret = EVENT_DONE;

  recursion++;

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    if (vc_handler) {
      ret = (this->*vc_handler)(event, data);
    } else {
      Debug("SocksProxy", "Ignore event = %s state = %d", get_vc_event_name(event), state);
    }
    break;

  case NET_EVENT_OPEN: {
    pending_action = nullptr;
    ink_assert(state == SERVER_TUNNEL);
    Debug("SocksProxy", "open to Socks server succeeded");

    NetVConnection *serverVC;
    serverVC = (NetVConnection *)data;

    OneWayTunnel *c_to_s = OneWayTunnel::OneWayTunnel_alloc();
    OneWayTunnel *s_to_c = OneWayTunnel::OneWayTunnel_alloc();

    c_to_s->init(clientVC, serverVC, nullptr, clientVIO, reader);
    s_to_c->init(serverVC, clientVC, /*aCont = */ nullptr, 0 /*best guess */, c_to_s->mutex.get());

    OneWayTunnel::SetupTwoWayTunnel(c_to_s, s_to_c);

    buf   = nullptr; // do not free buf. Tunnel will do that.
    state = ALL_DONE;
    break;
  }

  case NET_EVENT_OPEN_FAILED:
    pending_action = nullptr;
    vc_handler     = &SocksProxy::state_send_socks_reply;
    sendResp(false);
    state = RESP_TO_CLIENT;
    Debug("SocksProxy", "open to Socks server failed");
    break;

  case EVENT_INTERVAL:
    timeout = nullptr;
    Debug("SocksProxy", "SocksProxy timeout, state = %d", state);
    state = SOCKS_ERROR;
    break;

  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    Debug("SocksProxy", "VC_EVENT (state: %d error: %s)", state, get_vc_event_name(event));
    state = SOCKS_ERROR;
    break;

  default:
    ink_assert(!"bad case value\n");
    state = SOCKS_ERROR;
  }

  recursion--;

  if (state == SOCKS_ERROR) {
    if (pending_action) {
      pending_action->cancel();
      pending_action = nullptr;
    }

    if (timeout) {
      timeout->cancel(this);
      timeout = nullptr;
    }

    if (clientVC) {
      Debug("SocksProxy", "Closing clientVC on error");
      clientVC->do_io_close();
      clientVC = nullptr;
    }

    state = ALL_DONE;
  }

  if (state == ALL_DONE && recursion == 0) {
    free();
  }

  return ret;
}

int
SocksProxy::state_read_client_request(int event, void *data)
{
  ink_assert(state == SOCKS_ACCEPT);
  if (event != VC_EVENT_READ_READY) {
    ink_assert(!"not reached");
    return EVENT_CONT;
  }

  int64_t n = reader->block_read_avail();
  if (n < 2) {
    return EVENT_CONT;
  }

  unsigned char *p = (unsigned char *)reader->start();

  Debug("SocksProxy", "Accepted connection from a version %d client", (int)p[0]);

  switch (p[0]) {
  case SOCKS4_VERSION:
    version    = p[0];
    vc_handler = &SocksProxy::state_read_socks4_client_request;
    return (this->*vc_handler)(event, data);
    break;
  case SOCKS5_VERSION:
    version    = p[0];
    vc_handler = &SocksProxy::state_read_socks5_client_auth_methods;
    return (this->*vc_handler)(event, data);
    break;
  default:
    Warning("Wrong version for Socks: %d\n", (int)p[0]);
    state = SOCKS_ERROR;
    break;
  }

  return EVENT_DONE;
}

int
SocksProxy::state_read_socks4_client_request(int event, void *data)
{
  ink_assert(state == SOCKS_ACCEPT);

  int64_t n = reader->block_read_avail();
  /* Socks v4 request:
   * VN   CD   DSTPORT   DSTIP   USERID   NUL
   * 1  + 1  +  2      +  4    +  ?     +  1
   *
   * so the minimum length is 9 bytes
   */
  if (n < 9) {
    return EVENT_CONT;
  }

  unsigned char *p = (unsigned char *)reader->start();
  int i;
  // Skip UserID
  for (i = 8; i < n && p[i] != 0; i++)
    ;

  if (p[i] == 0) {
    port                      = p[2] * 256 + p[3];
    clientVC->socks_addr.type = SOCKS_ATYPE_IPV4;
    reader->consume(i + 1);
    state = AUTH_DONE;

    return parse_socks_client_request(p);
  } else {
    Debug("SocksProxy", "Need more data to parse userid for Socks: %d\n", p[0]);
    return EVENT_CONT;
  }
}

int
SocksProxy::state_read_socks5_client_auth_methods(int event, void *data)
{
  int64_t n;
  unsigned char *p;

  ink_assert(state == SOCKS_ACCEPT);

  n = reader->block_read_avail();
  p = (unsigned char *)reader->start();

  /* Socks v5 request:
   * VER   N_Methods   List_of Methods
   *  1  +  1        + (1 to N_Methods)
   *
   * so the minimum length is 2 + N_Methods bytes
   */
  if (n < 2 + p[1]) {
    return EVENT_CONT;
  }

  if (timeout) {
    timeout->cancel(this);
    timeout = nullptr;
  }

  auth_handler = &socks5ServerAuthHandler;
  /* disable further reads */
  clientVIO->nbytes = clientVIO->ndone;

  // There is some auth stuff left.
  if (invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_READ_COMPLETE, p) >= 0) {
    buf->reset();
    p = (unsigned char *)buf->start();

    int n_bytes = invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_FILL_WRITE_BUF, p);
    ink_assert(n_bytes > 0);

    buf->fill(n_bytes);

    vc_handler = &SocksProxy::state_send_socks5_auth_method;
    clientVC->do_io_write(this, n_bytes, reader, false);
  } else {
    Debug("SocksProxy", "Auth_handler returned error\n");
    state = SOCKS_ERROR;
  }

  return EVENT_DONE;
}

int
SocksProxy::state_send_socks5_auth_method(int event, void *data)
{
  ink_assert(state == SOCKS_ACCEPT);
  switch (event) {
  case VC_EVENT_WRITE_COMPLETE:
    state = AUTH_DONE;

    buf->reset();
    timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->socks_timeout));

    // We always send "No authentication is required" to client,
    // so the next is socks5 request.
    vc_handler = &SocksProxy::state_read_socks5_client_request;
    clientVC->do_io_read(this, INT64_MAX, buf);
    break;
  case VC_EVENT_WRITE_READY:
  default:
    Debug("SocksProxy", "Received unexpected event: %s\n", get_vc_event_name(event));
    break;
  }

  return EVENT_DONE;
}

int
SocksProxy::state_read_socks5_client_request(int event, void *data)
{
  int64_t n;
  unsigned char *p;

  ink_assert(state == AUTH_DONE);
  if (event != VC_EVENT_READ_READY) {
    ink_assert(!"not reached");
    return EVENT_CONT;
  }

  n = reader->block_read_avail();
  p = (unsigned char *)reader->start();

  /* Socks v5 request:
   * VER   CMD   RSV   ATYP   DST   DSTPORT
   *  1  +  1  +  1  +  1   +  ?  +  2
   *
   * so the minimum length is 6 + 4(IPv4) or 16(IPv6)
   */
  if (n <= 6) {
    return EVENT_CONT;
  }
  int req_len;
  switch (p[3]) {
  case SOCKS_ATYPE_IPV4:
    req_len = 10;
    break;
  case SOCKS_ATYPE_FQHN:
    req_len = 7 + p[4];
    break;
  case SOCKS_ATYPE_IPV6:
    req_len = 22;
    break;
  default:
    req_len = INT_MAX;
    state   = SOCKS_ERROR;
    Debug("SocksProxy", "Illegal address type(%d)", (int)p[3]);
  }

  if (state == SOCKS_ERROR) {
    return EVENT_DONE;
  } else if (n < req_len) {
    return EVENT_CONT;
  }

  port                      = p[req_len - 2] * 256 + p[req_len - 1];
  clientVC->socks_addr.type = p[3];
  auth_handler              = nullptr;
  reader->consume(req_len);

  return parse_socks_client_request(p);
}

int
SocksProxy::parse_socks_client_request(unsigned char *p)
{
  int ret = EVENT_DONE;

  if (timeout) {
    timeout->cancel(this);
    timeout = nullptr;
  }

  if (port == netProcessor.socks_conf_stuff->http_port && p[1] == SOCKS_CONNECT) {
    /* disable further reads */
    clientVIO->nbytes = clientVIO->ndone;

    ret        = setupHttpRequest(p);
    vc_handler = &SocksProxy::state_handing_over_http_request;
    sendResp(true);
    state = HTTP_REQ;
  } else {
    SOCKSPROXY_INC_STAT(socksproxy_tunneled_connections_stat);
    Debug("SocksProxy", "Tunnelling the connection for port %d", port);

    if (clientVC->socks_addr.type != SOCKS_ATYPE_IPV4) {
      // We dont support other kinds of addresses for tunnelling
      // if this is a hostname we could do host look up here
      ret = mainEvent(NET_EVENT_OPEN_FAILED, nullptr);
    } else {
      uint32_t ip;
      struct sockaddr_in addr;

      memcpy(&ip, &p[4], 4);
      ats_ip4_set(&addr, ip, htons(port));

      // Ignore further reads
      vc_handler = nullptr;

      state = SERVER_TUNNEL;

      // tunnel the connection.

      NetVCOptions vc_options;
      vc_options.socks_support = p[1];
      vc_options.socks_version = version;

      Action *action = netProcessor.connect_re(this, ats_ip_sa_cast(&addr), &vc_options);
      if (action != ACTION_RESULT_DONE) {
        ink_release_assert(pending_action == nullptr);
        pending_action = action;
      }
    }
  }

  return ret;
}

int
SocksProxy::state_handing_over_http_request(int event, void *data)
{
  int ret = EVENT_DONE;

  ink_assert(state == HTTP_REQ);

  switch (event) {
  case VC_EVENT_WRITE_COMPLETE: {
    HttpSessionAccept::Options ha_opt;

    SOCKSPROXY_INC_STAT(socksproxy_http_connections_stat);
    Debug("SocksProxy", "Handing over the HTTP request");

    ha_opt.transport_type = clientVC->attributes;
    HttpSessionAccept http_accept(ha_opt);
    if (!http_accept.accept(clientVC, buf, reader)) {
      state = SOCKS_ERROR;
    } else {
      state      = ALL_DONE;
      buf        = nullptr; // do not free buf. HttpSM will do that.
      clientVC   = nullptr;
      vc_handler = nullptr;
    }
    break;
  }
  case VC_EVENT_WRITE_READY:
    Debug("SocksProxy", "Received unexpected write_ready");
    ret = EVENT_CONT;
    break;
  }

  return ret;
}

int
SocksProxy::state_send_socks_reply(int event, void *data)
{
  int ret = EVENT_DONE;

  ink_assert(state == RESP_TO_CLIENT);

  switch (event) {
  case VC_EVENT_WRITE_COMPLETE:
    state = SOCKS_ERROR;
    break;
  case VC_EVENT_WRITE_READY:
    Debug("SocksProxy", "Received unexpected write_ready");
    ret = EVENT_CONT;
    break;
  }

  return ret;
}

int
SocksProxy::sendResp(bool granted)
{
  int n_bytes;

  // In SOCKS 4, IP addr and Dest Port fields are ignored.
  // In SOCKS 5, IP addr and Dest Port are the ones we use to connect to the
  // real host. In our case, it does not make sense, since we may not
  // connect at all. Set these fields to zeros. Any socks client which uses
  // these breaks caching.

  buf->reset();
  unsigned char *p = (unsigned char *)buf->start();

  if (version == SOCKS4_VERSION) {
    p[0]    = 0;
    p[1]    = (granted) ? SOCKS4_REQ_GRANTED : SOCKS4_CONN_FAILED;
    n_bytes = 8;
  } else {
    p[0] = SOCKS5_VERSION;
    p[1] = (granted) ? SOCKS5_REQ_GRANTED : SOCKS5_CONN_FAILED;
    p[2] = 0;
    p[3] = SOCKS_ATYPE_IPV4;
    p[4] = p[5] = p[6] = p[7] = p[8] = p[9] = 0;
    n_bytes                                 = 10;
  }

  buf->fill(n_bytes);
  clientVC->do_io_write(this, n_bytes, reader, false);

  return n_bytes;
}

int
SocksProxy::setupHttpRequest(unsigned char *p)
{
  int ret = EVENT_DONE;

  SocksAddrType *a = &clientVC->socks_addr;

  // read the ip addr buf
  // In both SOCKS4 and SOCKS5 addr starts after 4 octets
  switch (a->type) {
  case SOCKS_ATYPE_IPV4:
    a->addr.ipv4[0] = p[4];
    a->addr.ipv4[1] = p[5];
    a->addr.ipv4[2] = p[6];
    a->addr.ipv4[3] = p[7];
    break;

  case SOCKS_ATYPE_FQHN:
    // This is stored as a zero terminated string
    a->addr.buf = (unsigned char *)ats_malloc(p[4] + 1);
    memcpy(a->addr.buf, &p[5], p[4]);
    a->addr.buf[p[4]] = 0;
    break;
  case SOCKS_ATYPE_IPV6:
    // a->addr.buf = (unsigned char *)ats_malloc(16);
    // memcpy(a->addr.buf, &p[4], 16);
    // dont think we will use "proper" IPv6 addr anytime soon.
    // just use the last 4 octets as IPv4 addr:
    a->type         = SOCKS_ATYPE_IPV4;
    a->addr.ipv4[0] = p[16];
    a->addr.ipv4[1] = p[17];
    a->addr.ipv4[2] = p[18];
    a->addr.ipv4[3] = p[19];

    break;
  default:
    ink_assert(!"bad case value");
  }

  return ret;
}

static void
new_SocksProxy(NetVConnection *netVC)
{
  SocksProxy *proxy = socksProxyAllocator.alloc();
  proxy->init(netVC);
}

struct SocksAccepter : public Continuation {
  using SocksAccepterHandler = int (SocksAccepter::*)(int, void *);

  int
  mainEvent(int event, NetVConnection *netVC)
  {
    ink_assert(event == NET_EVENT_ACCEPT);
    // Debug("Socks", "Accepter got ACCEPT event");

    new_SocksProxy(netVC);

    return EVENT_CONT;
  }

  // There is no state used we dont need a mutex
  SocksAccepter() : Continuation(nullptr) { SET_HANDLER((SocksAccepterHandler)&SocksAccepter::mainEvent); }
};

void
start_SocksProxy(int port)
{
  Debug("SocksProxy", "Accepting SocksProxy connections on port %d", port);
  NetProcessor::AcceptOptions opt;
  opt.local_port = port;
  netProcessor.main_accept(new SocksAccepter(), NO_FD, opt);

  socksproxy_stat_block = RecAllocateRawStatBlock(socksproxy_stat_count);

  if (socksproxy_stat_block) {
    RecRegisterRawStat(socksproxy_stat_block, RECT_PROCESS, "proxy.process.socks.proxy.http_connections", RECD_INT, RECP_PERSISTENT,
                       socksproxy_http_connections_stat, RecRawStatSyncCount);

    RecRegisterRawStat(socksproxy_stat_block, RECT_PROCESS, "proxy.process.socks.proxy.tunneled_connections", RECD_INT,
                       RECP_PERSISTENT, socksproxy_tunneled_connections_stat, RecRawStatSyncCount);
  }
}

int
socks5ServerAuthHandler(int event, unsigned char *p, void (**h_ptr)(void))
{
  int ret = 0;

  switch (event) {
  case SOCKS_AUTH_READ_COMPLETE:

    ink_assert(p[0] == SOCKS5_VERSION);
    Debug("SocksProxy", "Socks read initial auth info");
    // do nothing
    break;

  case SOCKS_AUTH_FILL_WRITE_BUF:
    Debug("SocksProxy", "No authentication is required");
    p[0] = SOCKS5_VERSION;
    p[1] = 0; // no authentication necessary
    ret  = 2;
  // FALLTHROUGH
  case SOCKS_AUTH_WRITE_COMPLETE:
    // nothing to do
    *h_ptr = nullptr;
    break;

  default:
    ink_assert(!"bad case value");
    ret = -1;
  }

  return ret;
}
