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

#include "api/LifecycleAPIHooks.h"
#include "iocore/net/UDPNet.h"
#include "tscore/ink_config.h"
#include "../../iocore/net/P_SSLNextProtocolAccept.h"
#include "proxy/ProtocolProbeSessionAccept.h"
#include "proxy/ReverseProxy.h"
#include "proxy/http/HttpConfig.h"
#include "proxy/http/HttpProxyServerMain.h"
#include "proxy/http/HttpSessionAccept.h"
#include "proxy/http/HttpSessionManager.h"
#include "proxy/http/PreWarmManager.h"
#include "proxy/http2/Http2SessionAccept.h"
#ifdef USE_HTTP_DEBUG_LISTS
#include "proxy/http/Http1ClientSession.h"
#endif
#if TS_USE_QUIC == 1
#include "../../iocore/net/P_QUICNetProcessor.h"
#include "../../iocore/net/P_QUICNextProtocolAccept.h"
#include "proxy/http3/Http3SessionAccept.h"
#elif TS_USE_QMUX == 1
#include "proxy/http3/Http3SessionAccept.h"
#endif

#include <vector>

HttpSessionAccept                                    *plugin_http_accept             = nullptr;
HttpSessionAccept                                    *plugin_http_transparent_accept = nullptr;
extern std::function<PoolableSession *()>             create_h1_server_session;
extern std::function<PoolableSession *()>             create_h2_server_session;
extern std::map<int, std::function<ProxySession *()>> ProtocolSessionCreateMap;

static SLL<SSLNextProtocolAccept> ssl_plugin_acceptors;
static Ptr<ProxyMutex>            ssl_plugin_mutex;

std::mutex              proxyServerMutex;
std::condition_variable proxyServerCheck;
bool                    et_net_threads_ready = false;

std::mutex              etUdpMutex;
std::condition_variable etUdpCheck;
bool                    et_udp_threads_ready = false;

// File / process scope initializations
static bool HTTP_SERVER_INITIALIZED __attribute__((unused)) = []() -> bool {
  swoc::bwf::Global_Names().assign("ts-thread", [](swoc::BufferWriter &w, swoc::bwf::Spec const &spec) -> swoc::BufferWriter & {
    return bwformat(w, spec, this_thread());
  });
  swoc::bwf::Global_Names().assign("ts-ethread", [](swoc::BufferWriter &w, swoc::bwf::Spec const &spec) -> swoc::BufferWriter & {
    return bwformat(w, spec, this_ethread());
  });
  return true;
}();

bool
ssl_register_protocol(const char *protocol, Continuation *contp)
{
  SCOPED_MUTEX_LOCK(lock, ssl_plugin_mutex, this_ethread());

  for (SSLNextProtocolAccept *ssl = ssl_plugin_acceptors.head; ssl; ssl = ssl_plugin_acceptors.next(ssl)) {
    if (!ssl->registerEndpoint(protocol, contp)) {
      return false;
    }
  }

  return true;
}

/////////////////////////////////////////////////////////////////
//
//  main()
//
/////////////////////////////////////////////////////////////////

/** Data about an acceptor.

    This is used to separate setting up the proxy ports and
    starting to accept on them.

*/
struct HttpProxyAcceptor {
  /// Accept continuation.
  Continuation *_accept = nullptr;
  /// Options for @c NetProcessor.
  NetProcessor::AcceptOptions _net_opt;

  /// Default constructor.
  HttpProxyAcceptor() {}
};

/** Global acceptors.

    This is parallel to @c HttpProxyPort::global(), each generated
    from the corresponding port descriptor.

    @internal We use @c Continuation instead of @c HttpAccept because
    @c SSLNextProtocolAccept is a subclass of @c Cont instead of @c
    HttpAccept.
*/
std::vector<HttpProxyAcceptor> HttpProxyAcceptors;

// Called from InkAPI.cc
NetProcessor::AcceptOptions
make_net_accept_options(const HttpProxyPort *port, unsigned nthreads)
{
  NetProcessor::AcceptOptions net;

  net.accept_threads = nthreads;

  net.packet_mark   = RecGetRecordInt("proxy.config.net.sock_packet_mark_in").value_or(0);
  net.packet_tos    = RecGetRecordInt("proxy.config.net.sock_packet_tos_in").value_or(0);
  net.recv_bufsize  = RecGetRecordInt("proxy.config.net.sock_recv_buffer_size_in").value_or(0);
  net.send_bufsize  = RecGetRecordInt("proxy.config.net.sock_send_buffer_size_in").value_or(0);
  net.sockopt_flags = RecGetRecordInt("proxy.config.net.sock_option_flag_in").value_or(0);
  net.defer_accept  = RecGetRecordInt("proxy.config.net.defer_accept").value_or(0);

#if TCP_NOTSENT_LOWAT
  net.packet_notsent_lowat = RecGetRecordInt("proxy.config.net.sock_notsent_lowat").value_or(0);
#endif

#ifdef TCP_FASTOPEN
  net.tfo_queue_length = RecGetRecordInt("proxy.config.net.sock_option_tfo_queue_size_in").value_or(0);
#endif

  if (port) {
    net.f_inbound_transparent       = port->m_inbound_transparent_p;
    net.f_mptcp                     = port->m_mptcp;
    net.ip_family                   = port->m_family;
    net.local_port                  = port->m_port;
    net.f_proxy_protocol            = port->m_proxy_protocol;
    net.f_proxy_protocol_client_src = port->m_proxy_protocol_client_src;

    if (port->m_inbound_ip.isValid()) {
      net.local_ip = port->m_inbound_ip;
    } else if (AF_INET6 == port->m_family && HttpConfig::m_master.inbound.has_ip6()) {
      net.local_ip = HttpConfig::m_master.inbound.ip6().network_order();
    } else if (AF_INET == port->m_family && HttpConfig::m_master.inbound.has_ip4()) {
      net.local_ip = HttpConfig::m_master.inbound.ip4().network_order();
    } else if (AF_UNIX == port->m_family) {
      net.local_path = port->m_unix_path;
      net.unix_perm  = port->m_unix_perm;
      net.unix_uid   = port->m_unix_uid;
      net.unix_gid   = port->m_unix_gid;
      net.sockopt_flags &=
        ~(NetVCOptions::SOCK_OPT_NO_DELAY | NetVCOptions::SOCK_OPT_TCP_FAST_OPEN | NetVCOptions::SOCK_OPT_TCP_NOTSENT_LOWAT);
    }
  }
  return net;
}

static void
MakeHttpProxyAcceptor(HttpProxyAcceptor &acceptor, HttpProxyPort &port, unsigned nthreads)
{
  NetProcessor::AcceptOptions &net_opt    = acceptor._net_opt;
  auto                         accept_opt = std::make_shared<HttpSessionAccept::Options>();

  net_opt = make_net_accept_options(&port, nthreads);

  accept_opt->f_outbound_transparent = port.m_outbound_transparent_p;
  accept_opt->transport_type         = port.m_type;
  accept_opt->setHostResPreference(port.m_host_res_preference);
  accept_opt->setTransparentPassthrough(port.m_transparent_passthrough);
  accept_opt->setSessionProtocolPreference(port.m_session_protocol_preference);

  accept_opt->outbound += HttpConfig::m_master.outbound;
  accept_opt->outbound += port.m_outbound; // top priority, override master and base options.

  // OK the way this works is that the fallback for each port is a protocol
  // probe acceptor. For SSL ports, we can stack a NPN+ALPN acceptor in front
  // of that, and these ports will fall back to the probe if no NPN+ALPN endpoint
  // was negotiated.

  // XXX the protocol probe should be a configuration option.

  // A QUIC port is dispatched to the QUIC acceptor below, which has no probe fallback, so building a
  // probe for one only leaks it. Every other port type ends up behind the probe, either directly or
  // as the SSL acceptor's fallback. Without QUIC compiled in no port can be a QUIC port, so this is
  // always true there.
  bool const needs_probe = !port.isQUIC();

  ProtocolProbeSessionAccept *probe = nullptr;
  HttpSessionAccept          *http  = nullptr; // don't allocate this unless it will be used.

  if (needs_probe) {
    probe                       = new ProtocolProbeSessionAccept();
    probe->proxyPort            = &port;
    probe->proxy_protocol_ipmap = &HttpConfig::m_master.config_proxy_protocol_ip_addrs;

    if (port.m_session_protocol_preference.intersects(HTTP_PROTOCOL_SET)) {
      http = new HttpSessionAccept(accept_opt, &port);
      probe->registerEndpoint(ProtocolProbeSessionAccept::ProtoGroupKey::HTTP, http);
    }

    if (port.m_session_protocol_preference.intersects(HTTP2_PROTOCOL_SET)) {
      probe->registerEndpoint(ProtocolProbeSessionAccept::ProtoGroupKey::HTTP2, new Http2SessionAccept(accept_opt, &port));
    }
  }

  ProtocolSessionCreateMap.insert({TS_ALPN_PROTOCOL_INDEX_HTTP_1_0, create_h1_server_session});
  ProtocolSessionCreateMap.insert({TS_ALPN_PROTOCOL_INDEX_HTTP_1_1, create_h1_server_session});
  ProtocolSessionCreateMap.insert({TS_ALPN_PROTOCOL_INDEX_HTTP_2_0, create_h2_server_session});

  if (port.isSSL()) {
    SSLNextProtocolAccept *ssl = new SSLNextProtocolAccept(probe, port.m_transparent_passthrough, port.m_allow_plain);

    // ALPN selects the first server-offered protocol,
    // so make sure that we offer the newest protocol first.
    // But since registerEndpoint prepends you want to
    // register them backwards, so you'd want to register
    // the least important protocol first:
    // http/1.0, http/1.1, h2

    ssl->enableProtocols(port.m_session_protocol_preference);
    ssl->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_1_0, http);
    ssl->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_1_1, http);
    ssl->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_2_0, new Http2SessionAccept(accept_opt, &port));
#if TS_USE_QMUX
    ssl->registerEndpoint(TS_ALPN_PROTOCOL_H3QX, new Http3SessionAccept(accept_opt, &port));
#endif

    SCOPED_MUTEX_LOCK(lock, ssl_plugin_mutex, this_ethread());
    ssl_plugin_acceptors.push(ssl);
    ssl->proxyPort   = &port;
    acceptor._accept = ssl;
#if TS_USE_QUIC == 1
  } else if (port.isQUIC()) {
    QUICNextProtocolAccept *quic = new QUICNextProtocolAccept();

    quic->enableProtocols(port.m_session_protocol_preference);

    // HTTP/0.9 over QUIC draft-29 (for interop only, will be removed)
    quic->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_QUIC_D29, new Http3SessionAccept(accept_opt, &port));

    // HTTP/3 draft-29
    quic->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_3_D29, new Http3SessionAccept(accept_opt, &port));

    // HTTP/0.9 over QUIC (for interop only, will be removed)
    quic->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_QUIC, new Http3SessionAccept(accept_opt, &port));

    // HTTP/3
    quic->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_3, new Http3SessionAccept(accept_opt, &port));

    quic->proxyPort  = &port;
    acceptor._accept = quic;
#endif
  } else {
    acceptor._accept = probe;
  }
}

/// Do all pre-thread initialization / setup.
void
prep_HttpProxyServer()
{
  httpSessionManager.init();
}

/** Set up all the accepts and sockets.
 */
void
init_accept_HttpProxyServer(int n_accept_threads)
{
  HttpProxyPort::Group &proxy_ports = HttpProxyPort::global();

  init_reverse_proxy();

#ifdef USE_HTTP_DEBUG_LISTS
  ink_mutex_init(&debug_sm_list_mutex);
  ink_mutex_init(&debug_cs_list_mutex);
#endif

  // Used to give plugins the ability to create http requests
  //   The equivalent of the connecting to localhost on the  proxy
  //   port but without going through the operating system
  //
  if (plugin_http_accept == nullptr) {
    plugin_http_accept = new HttpSessionAccept();
  }

  // Same as plugin_http_accept except outbound transparent.
  if (!plugin_http_transparent_accept) {
    HttpSessionAccept::Options ha_opt;
    ha_opt.setOutboundTransparent(true);
    plugin_http_transparent_accept = new HttpSessionAccept(ha_opt);
  }

  if (!ssl_plugin_mutex) {
    ssl_plugin_mutex = new_ProxyMutex();
  }

  // Do the configuration defined ports.
  // Assign temporary empty objects of proxy ports size
  HttpProxyAcceptors.assign(proxy_ports.size(), HttpProxyAcceptor());
  for (int i = 0, n = proxy_ports.size(); i < n; ++i) {
    MakeHttpProxyAcceptor(HttpProxyAcceptors.at(i), proxy_ports[i], n_accept_threads);
  }
}

/** Increment the counter to keep track of how many et_net threads
 *  we have started. This function is scheduled at the start of each
 *  et_net thread using schedule_spawn(). We also check immediately
 *  after incrementing the counter to see whether all of the et_net
 *  threads have started such that we can notify main() to call
 *  start_HttpProxyServer().
 */
void
init_HttpProxyServer()
{
  if (eventProcessor.has_tg_started(ET_NET)) {
    std::unique_lock<std::mutex> lock(proxyServerMutex);
    et_net_threads_ready = true;
    lock.unlock();
    proxyServerCheck.notify_one();
  }

#if TS_USE_QUIC == 1
  if (eventProcessor.has_tg_started(ET_UDP)) {
    std::unique_lock<std::mutex> lock(etUdpMutex);
    et_udp_threads_ready = true;
    lock.unlock();
    etUdpCheck.notify_one();
  }
#endif
}

namespace
{

/** Open a loopback-bound companion listener for @a port, exempt from watchdog backpressure.
 *
 * Local connections arrive on the same port as all other traffic, so there is no port to exempt -
 * only a source address. But watchdog backpressure is a decision about a
 * listen socket, taken before @c accept4, and a connection's peer address is not knowable until after
 * it. The way out is to let the kernel do the discrimination: a socket bound explicitly to the
 * loopback address takes precedence over the wildcard socket on the same port, so local connections
 * land on this listener and everything from off box lands on the original one. Marking only this
 * listener exempt then protects local connections without giving up the backpressure for the flood.
 *
 * Opened whenever the watchdog is enabled at all, rather than when an active mode is selected:
 * `proxy.config.exec_thread.watchdog.mode` is reloadable, and binding or unbinding sockets in
 * response to a config change would be a great deal of risk for no benefit. An exempt loopback
 * listener behaves identically to the wildcard one when no backpressure is engaged.
 *
 * @param[in] acceptor The acceptor whose continuation and options this listener shares.
 * @param[in] port The proxy port being duplicated on loopback.
 */
void
start_loopback_companion_acceptor(HttpProxyAcceptor &acceptor, HttpProxyPort &port)
{
  NetProcessor::AcceptOptions net_opt = acceptor._net_opt;

  // localhost_only takes precedence over local_ip in accept_internal(), which is what binds this to
  // 127.0.0.1 or ::1 according to the port's address family.
  net_opt.localhost_only    = true;
  net_opt.f_watchdog_exempt = true;

  // Overlapping binds - this loopback socket and the port's wildcard socket - require SO_REUSEPORT on
  // both. The caller has already set it on the wildcard side.
  net_opt.f_force_reuseport = true;

  // Never take the proxy down over a supplementary listener. Without it the exemption is simply not
  // in effect, which the warning from Server::listen() reports.
  net_opt.f_non_fatal_listen = true;

  // Don't double the process's dedicated accept threads for a listener that will see only a low
  // volume of local connections. Left alone when accept threads are not in use, so that the companion follows
  // the same per-thread or SO_REUSEPORT model as the port it shadows.
  int accept_threads = net_opt.accept_threads;
  if (accept_threads < 0) {
    accept_threads = RecGetRecordInt("proxy.config.accept_threads").value_or(0);
  }
  if (accept_threads > 0) {
    net_opt.accept_threads = 1;
  }

  // A TLS port's companion has to go through the SSL processor so the loopback listener negotiates
  // exactly as the wildcard one does. Never inherit the pre-opened descriptor - that one belongs to
  // the wildcard listener.
  NetProcessor &np = port.isSSL() ? static_cast<NetProcessor &>(sslNetProcessor) : static_cast<NetProcessor &>(netProcessor);

  // A failed listen has already been warned about by Server::listen().
  if (nullptr == np.main_accept(acceptor._accept, ts::NO_FD, net_opt)) {
    return;
  }
  Note("opened watchdog-exempt loopback listener on port %d for local connections", port.m_port);
}

/** Whether @a port should get a loopback companion listener.
 *
 * Only plain TCP ports bound to a wildcard address qualify. A port already pinned to a specific
 * address was deliberately scoped by the operator, and adding a loopback bind to it would be new
 * exposure they did not ask for. QUIC is UDP and does not use this accept path at all, unix domain
 * sockets are local by construction, and a plugin port has no listen socket.
 */
bool
wants_loopback_companion(HttpProxyPort const &port)
{
  return !port.isQUIC() && !port.isPlugin() && AF_UNIX != port.m_family && !port.m_inbound_ip.isValid() &&
         !(AF_INET6 == port.m_family && HttpConfig::m_master.inbound.has_ip6()) &&
         !(AF_INET == port.m_family && HttpConfig::m_master.inbound.has_ip4());
}

} // namespace

void
start_HttpProxyServer()
{
  static bool           called_once = false;
  HttpProxyPort::Group &proxy_ports = HttpProxyPort::global();

  // The watchdog's shrink timeout is fixed at startup, so whether the exempt loopback listeners are
  // needed is decided once here.
  int watchdog_shrink_timeout_ms = RecGetRecordInt("proxy.config.exec_thread.watchdog.timeout_shrink_ms").value_or(0);

  ///////////////////////////////////
  // start accepting connections   //
  ///////////////////////////////////

  ink_assert(!called_once);
  ink_assert(proxy_ports.size() == HttpProxyAcceptors.size());

  for (int i = 0, n = proxy_ports.size(); i < n; ++i) {
    HttpProxyAcceptor &acceptor = HttpProxyAcceptors[i];
    HttpProxyPort     &port     = proxy_ports[i];

    // Decided before the port is opened, because an overlapping bind needs SO_REUSEPORT on *both*
    // sockets and it can only be set before bind().
    bool const companion = watchdog_shrink_timeout_ms > 0 && wants_loopback_companion(port);
    if (companion) {
      acceptor._net_opt.f_force_reuseport = true;
    }

    if (port.isSSL()) {
      if (nullptr == sslNetProcessor.main_accept(acceptor._accept, port.m_fd, acceptor._net_opt)) {
        return;
      }
#if TS_USE_QUIC == 1
    } else if (port.isQUIC()) {
      if (nullptr == quic_NetProcessor.main_accept(acceptor._accept, port.m_fd, acceptor._net_opt)) {
        return;
      }
#endif
    } else if (!port.isPlugin()) {
      if (nullptr == netProcessor.main_accept(acceptor._accept, port.m_fd, acceptor._net_opt)) {
        return;
      }
    }
    // XXX although we make a good pretence here, I don't believe that NetProcessor::main_accept() ever actually returns
    // NULL. It would be useful to be able to detect errors and spew them here though.

    if (companion) {
      start_loopback_companion_acceptor(acceptor, port);
    }
  }

  // Alert plugins that connections will be accepted.
  APIHook *hook = g_lifecycle_hooks->get(TS_LIFECYCLE_PORTS_READY_HOOK);
  while (hook) {
    hook->invoke(TS_EVENT_LIFECYCLE_PORTS_READY, nullptr);
    hook = hook->next();
  }

  prewarmManager.start();
}

void
stop_HttpProxyServer()
{
  sslNetProcessor.stop_accept();
  netProcessor.stop_accept();

  for (auto &acceptor : HttpProxyAcceptors) {
    delete acceptor._accept;
    acceptor._accept = nullptr;
  }
  HttpProxyAcceptors.clear();

  delete plugin_http_accept;
  plugin_http_accept = nullptr;

  delete plugin_http_transparent_accept;
  plugin_http_transparent_accept = nullptr;
}
