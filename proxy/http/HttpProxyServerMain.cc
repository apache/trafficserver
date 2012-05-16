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

#include "ink_config.h"
#include "P_Net.h"
#include "Main.h"
#include "Error.h"
#include "HttpConfig.h"
#include "HttpAccept.h"
#include "ReverseProxy.h"
#include "HttpSessionManager.h"
#include "HttpUpdateSM.h"
#include "HttpClientSession.h"
#include "HttpPages.h"
#include "HttpTunnel.h"
#include "Tokenizer.h"
#include "P_SSLNextProtocolAccept.h"

#ifdef DEBUG
extern "C"
{
  void http_dump();
}
///////////////////////////////////////////////////////////
//
//  http_dump()
//
///////////////////////////////////////////////////////////
void
http_dump()
{
//    if (diags->on("http_dump"))
  {
//      httpNetProcessor.dump();
//      HttpStateMachine::dump_state_machines();
  }

  return;
}
#endif
struct DumpStats: public Continuation
{
  int mainEvent(int event, void *e)
  {
    (void) event;
    (void) e;
    /* http_dump() */
//     dump_stats();
    return EVENT_CONT;
  }
  DumpStats():Continuation(NULL)
  {
    SET_HANDLER(&DumpStats::mainEvent);
  }
};

HttpAccept *plugin_http_accept = NULL;
HttpAccept *plugin_http_transparent_accept = 0;

#if !defined(TS_NO_API)
static SLL<SSLNextProtocolAccept> ssl_plugin_acceptors;
static ProcessMutex ssl_plugin_mutex;

bool
ssl_register_protocol(const char * protocol, Continuation * contp)
{
  ink_scoped_mutex lock(ssl_plugin_mutex);

  for (SSLNextProtocolAccept * ssl = ssl_plugin_acceptors.head;
        ssl; ssl = ssl_plugin_acceptors.next(ssl)) {
    if (!ssl->registerEndpoint(protocol, contp)) {
      return false;
    }
  }

  return true;
}

bool
ssl_unregister_protocol(const char * protocol, Continuation * contp)
{
  ink_scoped_mutex lock(ssl_plugin_mutex);

  for (SSLNextProtocolAccept * ssl = ssl_plugin_acceptors.head;
        ssl; ssl = ssl_plugin_acceptors.next(ssl)) {
    // Ignore possible failure because we want to try to unregister
    // from all SSL ports.
    ssl->unregisterEndpoint(protocol, contp);
  }

  return true;
}

#endif /* !defined(TS_NO_API) */

/////////////////////////////////////////////////////////////////
//
//  main()
//
/////////////////////////////////////////////////////////////////
void
init_HttpProxyServer(void)
{
#ifndef INK_NO_REVERSE
  init_reverse_proxy();
#endif
//  HttpConfig::startup();
  httpSessionManager.init();
  http_pages_init();
  ink_mutex_init(&debug_sm_list_mutex, "HttpSM Debug List");
  ink_mutex_init(&debug_cs_list_mutex, "HttpCS Debug List");
  // DI's request to disable/reenable ICP on the fly
  icp_dynamic_enabled = 1;
  // INKqa11918
  init_max_chunk_buf();

#ifndef TS_NO_API
  // Used to give plugins the ability to create http requests
  //   The equivalent of the connecting to localhost on the  proxy
  //   port but without going through the operating system
  //
  if (plugin_http_accept == NULL) {
    plugin_http_accept = NEW(new HttpAccept);
    plugin_http_accept->mutex = new_ProxyMutex();
  }
  // Same as plugin_http_accept except outbound transparent.
  if (! plugin_http_transparent_accept) {
    HttpAccept::Options ha_opt;
    ha_opt.setOutboundTransparent(true);
    plugin_http_transparent_accept = NEW(new HttpAccept(ha_opt));
    plugin_http_transparent_accept->mutex = new_ProxyMutex();
  }
  ink_mutex_init(&ssl_plugin_mutex, "SSL Acceptor List");
#endif
}


void
start_HttpProxyServer(int accept_threads)
{
  char *dump_every_str = 0;
  static bool called_once = false;
  NetProcessor::AcceptOptions opt;

  if ((dump_every_str = getenv("PROXY_DUMP_STATS")) != 0) {
    int dump_every_sec = atoi(dump_every_str);
    eventProcessor.schedule_every(NEW(new DumpStats), HRTIME_SECONDS(dump_every_sec), ET_CALL);
  }

  ///////////////////////////////////
  // start accepting connections   //
  ///////////////////////////////////

  ink_assert(!called_once);

  opt.accept_threads = accept_threads;
  REC_ReadConfigInteger(opt.recv_bufsize, "proxy.config.net.sock_recv_buffer_size_in");
  REC_ReadConfigInteger(opt.send_bufsize, "proxy.config.net.sock_send_buffer_size_in");
  REC_ReadConfigInteger(opt.packet_mark, "proxy.config.net.sock_packet_mark_in");
  REC_ReadConfigInteger(opt.packet_tos, "proxy.config.net.sock_packet_tos_in");
  SslConfigParams *sslParam = sslTerminationConfig.acquire();
  
  for ( int i = 0 , n = HttpProxyPort::global().length() ; i < n ; ++i ) {
    HttpProxyPort& p = HttpProxyPort::global()[i];
    HttpAccept::Options ha_opt;

    opt.f_inbound_transparent = p.m_inbound_transparent_p;
    opt.ip_family = p.m_family;
    opt.local_port = p.m_port;

    ha_opt.f_outbound_transparent = p.m_outbound_transparent_p;
    ha_opt.transport_type = p.m_type;

    if (p.m_inbound_ip.isValid())
      opt.local_ip = p.m_inbound_ip;
    else if (AF_INET6 == p.m_family && HttpConfig::m_master.inbound_ip6.isIp6())
      opt.local_ip = HttpConfig::m_master.inbound_ip6;
    else if (AF_INET == p.m_family && HttpConfig::m_master.inbound_ip4.isIp4())
      opt.local_ip = HttpConfig::m_master.inbound_ip4;

    if (p.m_outbound_ip4.isValid())
      ha_opt.outbound_ip4 = p.m_outbound_ip4;
    else if (HttpConfig::m_master.outbound_ip4.isValid())
      ha_opt.outbound_ip4 = HttpConfig::m_master.outbound_ip4;

    if (p.m_outbound_ip6.isValid())
      ha_opt.outbound_ip6 = p.m_outbound_ip6;
    else if (HttpConfig::m_master.outbound_ip6.isValid())
      ha_opt.outbound_ip6 = HttpConfig::m_master.outbound_ip6;

    if (p.isSSL()) {
      HttpAccept * http = NEW(new HttpAccept(ha_opt));
      SSLNextProtocolAccept * ssl = NEW(new SSLNextProtocolAccept(http));
      ssl->registerEndpoint(TS_NPN_PROTOCOL_HTTP_1_0, http);
      ssl->registerEndpoint(TS_NPN_PROTOCOL_HTTP_1_1, http);

#ifndef TS_NO_API
      ink_scoped_mutex lock(ssl_plugin_mutex);
      ssl_plugin_acceptors.push(ssl);
#endif
      sslNetProcessor.main_accept(ssl, p.m_fd, opt);
    } else {
      netProcessor.main_accept(NEW(new HttpAccept(ha_opt)), p.m_fd, opt);
    }
  }

  sslTerminationConfig.release(sslParam);

#ifdef DEBUG
  if (diags->on("http_dump")) {
//      HttpStateMachine::dump_state_machines();
  }
#endif
#if TS_HAS_TESTS
  if (is_action_tag_set("http_update_test")) {
    init_http_update_test();
  }
#endif
}

void
start_HttpProxyServerBackDoor(int port, int accept_threads)
{
  NetProcessor::AcceptOptions opt;
  HttpAccept::Options ha_opt;

  opt.local_port = port;
  opt.accept_threads = accept_threads;
  opt.localhost_only = true;
  ha_opt.backdoor = true;
  
  // The backdoor only binds the loopback interface
  netProcessor.main_accept(NEW(new HttpAccept(ha_opt)), NO_FD, opt);
}
