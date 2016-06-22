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
  This implements SOCKS server. We intecept the http traffic and send it
  through HTTP. Others are tunneled through directly to the socks server.


*/
#include "ts/ink_platform.h"
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

struct SocksProxy : public Continuation {
  typedef int (SocksProxy::*EventHandler)(int event, void *data);

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

  SocksProxy()
    : Continuation(),
      clientVC(0),
      clientVIO(0),
      buf(0),
      timeout(0),
      auth_handler(0),
      version(0),
      state(SOCKS_INIT),
      recursion(0),
      pending_action(NULL)
  {
  }
  ~SocksProxy() {}
  // int startEvent(int event, void * data);
  int mainEvent(int event, void *data);
  int setupHttpRequest(unsigned char *p);
  // int setupServerTunnel(unsigned char * p);

  int sendResp(bool granted);

  void init(NetVConnection *netVC);
  void free();

private:
  NetVConnection *clientVC;
  VIO *clientVIO;

  MIOBuffer *buf;
  IOBufferReader *reader;
  Event *timeout;

  SocksAuthHandler auth_handler;

  unsigned char version;

  int state;
  int recursion;
  Action *pending_action;
};

ClassAllocator<SocksProxy> socksProxyAllocator("socksProxyAllocator");

void
SocksProxy::init(NetVConnection *netVC)
{
  mutex  = new_ProxyMutex();
  buf    = new_MIOBuffer();
  reader = buf->alloc_reader();

  SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());

  SET_HANDLER((EventHandler)&SocksProxy::mainEvent);

  mainEvent(NET_EVENT_ACCEPT, netVC);
}

void
SocksProxy::free()
{
  if (buf)
    free_MIOBuffer(buf);

  mutex = NULL;

  socksProxyAllocator.free(this);
}

int
SocksProxy::mainEvent(int event, void *data)
{
  int ret = EVENT_DONE;
  unsigned char *p;

  VIO *vio;
  int64_t n_read_avail;
  // int n_consume;

  recursion++;

  switch (event) {
  case NET_EVENT_ACCEPT:
    state = SOCKS_ACCEPT;
    Debug("SocksProxy", "Proxy got accept event\n");

    clientVC = (NetVConnection *)data;
    clientVC->socks_addr.reset();
  // Fall through:
  case VC_EVENT_WRITE_COMPLETE:

    switch (state) {
    case HTTP_REQ: {
      HttpSessionAccept::Options ha_opt;
      // This is a WRITE_COMPLETE. vio->nbytes == vio->ndone is true

      SOCKSPROXY_INC_STAT(socksproxy_http_connections_stat);
      Debug("SocksProxy", "Handing over the HTTP request\n");

      ha_opt.transport_type = clientVC->attributes;
      HttpSessionAccept http_accept(ha_opt);
      http_accept.mainEvent(NET_EVENT_ACCEPT, clientVC);
      state = ALL_DONE;
      break;
    }

    case RESP_TO_CLIENT:
      state = SOCKS_ERROR;
      break;

    default:
      buf->reset();
      timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->socks_timeout));
      clientVC->do_io_read(this, INT64_MAX, buf);
    }

    break;

  case VC_EVENT_WRITE_READY:
    Debug("SocksProxy", "Received unexpected write_ready\n");
    break;

  case VC_EVENT_READ_COMPLETE:
    Debug("SocksProxy", "Oops! We should never get Read_Complete.\n");
  // FALLTHROUGH
  case VC_EVENT_READ_READY: {
    unsigned char *port_ptr = 0;

    ret = EVENT_CONT;
    vio = (VIO *)data;

    n_read_avail = reader->block_read_avail();
    ink_assert(n_read_avail == reader->read_avail());
    p = (unsigned char *)reader->start();

    if (n_read_avail >= 2) {
      Debug(state == SOCKS_ACCEPT ? "SocksProxy" : "", "Accepted connection from a version %d client\n", (int)p[0]);

      // Most of the time request is just a single packet
      switch (p[0]) {
      case SOCKS4_VERSION:
        ink_assert(state == SOCKS_ACCEPT);

        if (n_read_avail > 8) {
          // read the user name
          int i = 8;
          while (p[i] != 0 && n_read_avail > i)
            i++;

          if (p[i] == 0) {
            port_ptr                  = &p[2];
            clientVC->socks_addr.type = SOCKS_ATYPE_IPV4;
            reader->consume(i + 1);
            // n_consume = i+1;
            ret = EVENT_DONE;
          }
        }
        break;

      case SOCKS5_VERSION:

        if (state == SOCKS_ACCEPT) {
          if (n_read_avail >= 2 + p[1]) {
            auth_handler = &socks5ServerAuthHandler;
            ret          = EVENT_DONE;
          }
        } else {
          ink_assert(state == AUTH_DONE);

          if (n_read_avail >= 5) {
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
              Debug("SocksProxy", "Illegal address type(%d)\n", (int)p[3]);
            }

            if (n_read_avail >= req_len) {
              port_ptr                  = &p[req_len - 2];
              clientVC->socks_addr.type = p[3];
              auth_handler              = NULL;
              reader->consume(req_len);
              ret = EVENT_DONE;
            }
          }
        }
        break;

      default:
        Warning("Wrong version for Socks: %d\n", p[0]);
        state = SOCKS_ERROR;
      }
    }

    if (ret == EVENT_DONE) {
      timeout->cancel(this);
      timeout = 0;

      if (auth_handler) {
        /* disable further reads */
        vio->nbytes = vio->ndone;

        // There is some auth stuff left.
        if (invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_READ_COMPLETE, p) >= 0) {
          buf->reset();
          p = (unsigned char *)buf->start();

          int n_bytes = invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_FILL_WRITE_BUF, p);
          ink_assert(n_bytes > 0);

          buf->fill(n_bytes);

          clientVC->do_io_write(this, n_bytes, reader, 0);

          state = AUTH_DONE;
        } else {
          Debug("SocksProxy", "Auth_handler returned error\n");
          state = SOCKS_ERROR;
        }

      } else {
        int port = port_ptr[0] * 256 + port_ptr[1];
        version  = p[0];

        if (port == netProcessor.socks_conf_stuff->http_port && p[1] == SOCKS_CONNECT) {
          /* disable further reads */
          vio->nbytes = vio->ndone;

          ret = setupHttpRequest(p);
          sendResp(true);
          state = HTTP_REQ;

        } else {
          SOCKSPROXY_INC_STAT(socksproxy_tunneled_connections_stat);
          Debug("SocksProxy", "Tunnelling the connection for port %d", port);

          if (clientVC->socks_addr.type != SOCKS_ATYPE_IPV4) {
            // We dont support other kinds of addresses for tunnelling
            // if this is a hostname we could do host look up here
            mainEvent(NET_EVENT_OPEN_FAILED, NULL);
            break;
          }

          uint32_t ip;
          struct sockaddr_in addr;

          memcpy(&ip, &p[4], 4);
          ats_ip4_set(&addr, ip, htons(port));

          state     = SERVER_TUNNEL;
          clientVIO = vio; // used in the tunnel

          // tunnel the connection.

          NetVCOptions vc_options;
          vc_options.socks_support = p[1];
          vc_options.socks_version = version;

          Action *action = netProcessor.connect_re(this, ats_ip_sa_cast(&addr), &vc_options);
          if (action != ACTION_RESULT_DONE) {
            ink_assert(pending_action == NULL);
            pending_action = action;
          }
        }
      }
    } // if (ret == EVENT_DONE)

    break;
  }

  case NET_EVENT_OPEN: {
    pending_action = NULL;
    ink_assert(state == SERVER_TUNNEL);
    Debug("SocksProxy", "open to Socks server succeeded\n");

    NetVConnection *serverVC;
    serverVC = (NetVConnection *)data;

    OneWayTunnel *c_to_s = OneWayTunnel::OneWayTunnel_alloc();
    OneWayTunnel *s_to_c = OneWayTunnel::OneWayTunnel_alloc();

    c_to_s->init(clientVC, serverVC, NULL, clientVIO, reader);
    s_to_c->init(serverVC, clientVC, /*aCont = */ NULL, 0 /*best guess */, c_to_s->mutex);

    OneWayTunnel::SetupTwoWayTunnel(c_to_s, s_to_c);

    buf   = 0; // do not free buf. Tunnel will do that.
    state = ALL_DONE;
    break;
  }

  case NET_EVENT_OPEN_FAILED:
    pending_action = NULL;
    sendResp(false);
    state = RESP_TO_CLIENT;
    Debug("SocksProxy", "open to Socks server failed\n");
    break;

  case EVENT_INTERVAL:
    timeout = 0;
    Debug("SocksProxy", "SocksProxy timeout, state = %d\n", state);
    state = SOCKS_ERROR;
    break;

  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_EOS:
    Debug("SocksProxy", "VC_EVENT (state: %d error: %s)\n", state, get_vc_event_name(event));
    state = SOCKS_ERROR;
    break;

  default:
    ink_assert(!"bad case value\n");
    state = SOCKS_ERROR;
  }

  if (state == SOCKS_ERROR) {
    if (pending_action)
      pending_action->cancel();

    if (timeout)
      timeout->cancel(this);

    if (clientVC) {
      Debug("SocksProxy", "Closing clientVC on error\n");
      clientVC->do_io_close();
      clientVC = NULL;
    }

    state = ALL_DONE;
  }

  recursion--;

  if (state == ALL_DONE && recursion == 0) {
    free();
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
  // connect at all. Set these feilds to zeros. Any socks client which uses
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
  clientVC->do_io_write(this, n_bytes, reader, 0);

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
    // This is stored as a zero terminicated string
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
  typedef int (SocksAccepter::*SocksAccepterHandler)(int, void *);

  int
  mainEvent(int event, NetVConnection *netVC)
  {
    ink_assert(event == NET_EVENT_ACCEPT);
    // Debug("Socks", "Accepter got ACCEPT event\n");

    new_SocksProxy(netVC);

    return EVENT_CONT;
  }

  // There is no state used we dont need a mutex
  SocksAccepter() : Continuation(NULL) { SET_HANDLER((SocksAccepterHandler)&SocksAccepter::mainEvent); }
};

void
start_SocksProxy(int port)
{
  Debug("SocksProxy", "Accepting SocksProxy connections on port %d\n", port);
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
  // bool no_auth_needed=true;

  switch (event) {
  case SOCKS_AUTH_READ_COMPLETE:

    ink_assert(p[0] == SOCKS5_VERSION);
    Debug("SocksProxy", "Socks read initial auth info\n");
    // do nothing
    break;

  case SOCKS_AUTH_FILL_WRITE_BUF:
    Debug("SocksProxy", "No authentication is required\n");
    p[0] = SOCKS5_VERSION;
    p[1] = 0; // no authentication necessary
    ret  = 2;
  // FALLTHROUGH
  case SOCKS_AUTH_WRITE_COMPLETE:
    // nothing to do
    *h_ptr = NULL;
    break;

  default:
    ink_assert(!"bad case value");
    ret = -1;
  }

  return ret;
}
