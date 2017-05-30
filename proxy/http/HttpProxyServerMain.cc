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

#include "ts/ink_config.h"
#include "P_Net.h"
#include "Main.h"
#include "HttpConfig.h"
#include "HttpSessionAccept.h"
#include "ReverseProxy.h"
#include "HttpSessionManager.h"
#include "HttpUpdateSM.h"
#ifdef USE_HTTP_DEBUG_LISTS
#include "Http1ClientSession.h"
#endif
#include "HttpPages.h"
#include "HttpTunnel.h"
#include "ts/Tokenizer.h"
#include "P_SSLNextProtocolAccept.h"
#include "ProtocolProbeSessionAccept.h"
#include "http2/Http2SessionAccept.h"

HttpSessionAccept *plugin_http_accept             = nullptr;
HttpSessionAccept *plugin_http_transparent_accept = nullptr;

static SLL<SSLNextProtocolAccept> ssl_plugin_acceptors;
static Ptr<ProxyMutex> ssl_plugin_mutex;

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

bool
ssl_unregister_protocol(const char *protocol, Continuation *contp)
{
  SCOPED_MUTEX_LOCK(lock, ssl_plugin_mutex, this_ethread());

  for (SSLNextProtocolAccept *ssl = ssl_plugin_acceptors.head; ssl; ssl = ssl_plugin_acceptors.next(ssl)) {
    // Ignore possible failure because we want to try to unregister
    // from all SSL ports.
    ssl->unregisterEndpoint(protocol, contp);
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
  Continuation *_accept;
  /// Options for @c NetProcessor.
  NetProcessor::AcceptOptions _net_opt;

  /// Default constructor.
  HttpProxyAcceptor() : _accept(nullptr) {}
};

/** Global acceptors.

    This is parallel to @c HttpProxyPort::global(), each generated
    from the corresponding port descriptor.

    @internal We use @c Continuation instead of @c HttpAccept because
    @c SSLNextProtocolAccept is a subclass of @c Cont instead of @c
    HttpAccept.
*/
Vec<HttpProxyAcceptor> HttpProxyAcceptors;

// Called from InkAPI.cc
NetProcessor::AcceptOptions
make_net_accept_options(const HttpProxyPort *port, unsigned nthreads)
{
  NetProcessor::AcceptOptions net;

  net.accept_threads = nthreads;

  REC_ReadConfigInteger(net.packet_mark, "proxy.config.net.sock_packet_mark_in");
  REC_ReadConfigInteger(net.packet_tos, "proxy.config.net.sock_packet_tos_in");
  REC_ReadConfigInteger(net.recv_bufsize, "proxy.config.net.sock_recv_buffer_size_in");
  REC_ReadConfigInteger(net.send_bufsize, "proxy.config.net.sock_send_buffer_size_in");
  REC_ReadConfigInteger(net.sockopt_flags, "proxy.config.net.sock_option_flag_in");

#ifdef TCP_FASTOPEN
  REC_ReadConfigInteger(net.tfo_queue_length, "proxy.config.net.sock_option_tfo_queue_size_in");
#endif

  if (port) {
    net.f_inbound_transparent = port->m_inbound_transparent_p;
    net.ip_family             = port->m_family;
    net.local_port            = port->m_port;

    if (port->m_inbound_ip.isValid()) {
      net.local_ip = port->m_inbound_ip;
    } else if (AF_INET6 == port->m_family && HttpConfig::m_master.inbound_ip6.isIp6()) {
      net.local_ip = HttpConfig::m_master.inbound_ip6;
    } else if (AF_INET == port->m_family && HttpConfig::m_master.inbound_ip4.isIp4()) {
      net.local_ip = HttpConfig::m_master.inbound_ip4;
    }
  }

  return net;
}

static void
MakeHttpProxyAcceptor(HttpProxyAcceptor &acceptor, HttpProxyPort &port, unsigned nthreads)
{
  NetProcessor::AcceptOptions &net_opt = acceptor._net_opt;
  HttpSessionAccept::Options accept_opt;

  net_opt = make_net_accept_options(&port, nthreads);

  accept_opt.f_outbound_transparent = port.m_outbound_transparent_p;
  accept_opt.transport_type         = port.m_type;
  accept_opt.setHostResPreference(port.m_host_res_preference);
  accept_opt.setTransparentPassthrough(port.m_transparent_passthrough);
  accept_opt.setSessionProtocolPreference(port.m_session_protocol_preference);

  if (port.m_outbound_ip4.isValid()) {
    accept_opt.outbound_ip4 = port.m_outbound_ip4;
  } else if (HttpConfig::m_master.outbound_ip4.isValid()) {
    accept_opt.outbound_ip4 = HttpConfig::m_master.outbound_ip4;
  }

  if (port.m_outbound_ip6.isValid()) {
    accept_opt.outbound_ip6 = port.m_outbound_ip6;
  } else if (HttpConfig::m_master.outbound_ip6.isValid()) {
    accept_opt.outbound_ip6 = HttpConfig::m_master.outbound_ip6;
  }

  // OK the way this works is that the fallback for each port is a protocol
  // probe acceptor. For SSL ports, we can stack a NPN+ALPN acceptor in front
  // of that, and these ports will fall back to the probe if no NPN+ALPN endpoint
  // was negotiated.

  // XXX the protocol probe should be a configuration option.

  ProtocolProbeSessionAccept *probe = new ProtocolProbeSessionAccept();
  HttpSessionAccept *http           = nullptr; // don't allocate this unless it will be used.
  probe->proxyPort                  = &port;

  if (port.m_session_protocol_preference.intersects(HTTP_PROTOCOL_SET)) {
    http = new HttpSessionAccept(accept_opt);
    probe->registerEndpoint(ProtocolProbeSessionAccept::PROTO_HTTP, http);
  }

  if (port.m_session_protocol_preference.intersects(HTTP2_PROTOCOL_SET)) {
    probe->registerEndpoint(ProtocolProbeSessionAccept::PROTO_HTTP2, new Http2SessionAccept(accept_opt));
  }

  if (port.isSSL()) {
    SSLNextProtocolAccept *ssl = new SSLNextProtocolAccept(probe, port.m_transparent_passthrough);

    // ALPN selects the first server-offered protocol,
    // so make sure that we offer the newest protocol first.
    // But since registerEndpoint prepends you want to
    // register them backwards, so you'd want to register
    // the least important protocol first:
    // http/1.0, http/1.1, h2

    // HTTP
    if (port.m_session_protocol_preference.contains(TS_ALPN_PROTOCOL_INDEX_HTTP_1_0)) {
      ssl->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_1_0, http);
    }

    if (port.m_session_protocol_preference.contains(TS_ALPN_PROTOCOL_INDEX_HTTP_1_1)) {
      ssl->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_1_1, http);
    }

    // HTTP2
    if (port.m_session_protocol_preference.contains(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0)) {
      Http2SessionAccept *acc = new Http2SessionAccept(accept_opt);

      ssl->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_2_0, acc);
    }

    SCOPED_MUTEX_LOCK(lock, ssl_plugin_mutex, this_ethread());
    ssl_plugin_acceptors.push(ssl);
    ssl->proxyPort   = &port;
    acceptor._accept = ssl;
  } else {
    acceptor._accept = probe;
  }
}

/** Set up all the accepts and sockets.
 */
void
init_HttpProxyServer(int n_accept_threads)
{
  HttpProxyPort::Group &proxy_ports = HttpProxyPort::global();

  init_reverse_proxy();
  httpSessionManager.init();
  http_pages_init();

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
  for (int i = 0, n = proxy_ports.length(); i < n; ++i) {
    MakeHttpProxyAcceptor(HttpProxyAcceptors.add(), proxy_ports[i], n_accept_threads);
  }
}

void
start_HttpProxyServer()
{
  static bool called_once           = false;
  HttpProxyPort::Group &proxy_ports = HttpProxyPort::global();

  ///////////////////////////////////
  // start accepting connections   //
  ///////////////////////////////////

  ink_assert(!called_once);
  ink_assert(proxy_ports.length() == HttpProxyAcceptors.length());

  for (int i = 0, n = proxy_ports.length(); i < n; ++i) {
    HttpProxyAcceptor &acceptor = HttpProxyAcceptors[i];
    HttpProxyPort &port         = proxy_ports[i];
    if (port.isSSL()) {
      if (nullptr == sslNetProcessor.main_accept(acceptor._accept, port.m_fd, acceptor._net_opt)) {
        return;
      }
    } else if (!port.isPlugin()) {
      if (nullptr == netProcessor.main_accept(acceptor._accept, port.m_fd, acceptor._net_opt)) {
        return;
      }
    }
    // XXX although we make a good pretence here, I don't believe that NetProcessor::main_accept() ever actually returns
    // NULL. It would be useful to be able to detect errors and spew them here though.
  }

#if TS_HAS_TESTS
  if (is_action_tag_set("http_update_test")) {
    init_http_update_test();
  }
#endif

  // Alert plugins that connections will be accepted.
  APIHook *hook = lifecycle_hooks->get(TS_LIFECYCLE_PORTS_READY_HOOK);
  while (hook) {
    hook->invoke(TS_EVENT_LIFECYCLE_PORTS_READY, nullptr);
    hook = hook->next();
  }
}

void
start_HttpProxyServerBackDoor(int port, int accept_threads)
{
  NetProcessor::AcceptOptions opt;
  HttpSessionAccept::Options ha_opt;

  opt.local_port     = port;
  opt.accept_threads = accept_threads;
  opt.localhost_only = true;
  ha_opt.backdoor    = true;
  opt.backdoor       = true;

  // The backdoor only binds the loopback interface
  netProcessor.main_accept(new HttpSessionAccept(ha_opt), NO_FD, opt);
}
