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

HttpEntryPoint *http_open_port_array = NULL;
HttpEntryPoint *http_other_port_array = NULL;

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

// Does not modify result->port
void get_connection_attributes(const char *attr, HttpEntryPoint *result) {
  int attr_len;

  result->type = SERVER_PORT_DEFAULT;
  result->domain = AF_INET;

  if (!attr ) return;

  attr_len = strlen(attr);

  if (attr_len > 2) {
    Warning("too many port attributes: '%s'", attr);
    return;
  } else if (attr_len <= 0) {
    return;
  }

  switch (*attr) {
  case 'S' : result->type = SERVER_PORT_SSL; break;
  case 'C': result->type = SERVER_PORT_COMPRESSED; break;
  case '<':
    result->f_outbound_transparent = true;
    result->type = SERVER_PORT_DEFAULT;
    break;
  case '=':
    result->f_outbound_transparent = true;
    result->f_inbound_transparent = true;
    result->type = SERVER_PORT_DEFAULT;
    break;
  case '>':
    result->f_inbound_transparent = true;
    result->type = SERVER_PORT_DEFAULT;
    break;
  case 'X': result->type = SERVER_PORT_DEFAULT; break;
  case 'T': result->type = SERVER_PORT_BLIND_TUNNEL; break;
  default: Warning("unknown port attribute '%s'", attr); break;
  }

  if (attr_len >= 2) {
    switch (*(attr + 1)) {
    case '6': result->domain = AF_INET6; break;
    default: result->domain = AF_INET;
    }
  }
}


static HttpEntryPoint *
parse_http_server_other_ports()
{
  int list_entries;
  int accept_index = 0;
  int port = 0;
  char *other_ports_str = NULL;
  Tokenizer listTok(", ");
  HttpEntryPoint *additional_ports_array;

  other_ports_str = HTTP_ConfigReadString("proxy.config.http.server_other_ports");

  if (!other_ports_str || *other_ports_str == '\0') {
    return NULL;
  }

  list_entries = listTok.Initialize(other_ports_str, SHARE_TOKS);

  if (list_entries <= 0) return 0;

  // Add one so last entry is marked with @a fd of @c NO_FD
  additional_ports_array = new HttpEntryPoint[list_entries + 1];

  for (int i = 0; i < list_entries; ++i) {
    HttpEntryPoint* pent = additional_ports_array + accept_index;
    char const* cur_entry = listTok[i];
    char* next;

    // Check to see if there is a port attribute
    char const* attr_str = strchr(cur_entry, ':');
    if (attr_str != NULL) attr_str = attr_str + 1;

    // Port value
    port = strtoul(cur_entry, &next, 10);
    if (next == cur_entry) {
      Warning("failed to read accept port '%s', discarding", cur_entry);
      continue;
    } else if (!(1 <= port || port <= 65535)) {
      Warning("Port value '%s' out of range, discarding", cur_entry);
      continue;
    }

    pent->port = port;
    get_connection_attributes(attr_str, pent);
    ++accept_index;
  }

  ink_assert(accept_index < list_entries + 1);

  additional_ports_array[accept_index].port = -1;

  return additional_ports_array;
}

HttpAccept *plugin_http_accept = NULL;

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
    plugin_http_accept = NEW(new HttpAccept(SERVER_PORT_DEFAULT, false));
    plugin_http_accept->mutex = new_ProxyMutex();
  }
#endif
}


void
start_HttpProxyServer(int fd, int port, int ssl_fd, int accept_threads)
{
  char *dump_every_str = 0;
  static bool called_once = false;
  NetProcessor::AcceptOptions opt;

  ////////////////////////////////
  // check if accept port is in //
  // network safe range.        //
  ////////////////////////////////
  if ((port<1) || (port> 65535)) {

    ProcessFatal("accept port %d is not between 1 and 65535 ", "please check configuration", port);
    return;
  }


  if ((dump_every_str = getenv("PROXY_DUMP_STATS")) != 0) {
    int dump_every_sec = atoi(dump_every_str);
    eventProcessor.schedule_every(NEW(new DumpStats), HRTIME_SECONDS(dump_every_sec), ET_CALL);
  }

  ///////////////////////////////////
  // start accepting connections   //
  ///////////////////////////////////

  ink_assert(!called_once);

  opt.accept_threads = accept_threads;

  // If ports are already open, just listen on those and ignore other
  // configuration.
  if (http_open_port_array) {
    for ( HttpEntryPoint* pent = http_open_port_array
        ; ts::NO_FD != pent->fd
        ; ++pent
    ) {
      opt.f_outbound_transparent = pent->f_outbound_transparent;
      opt.f_inbound_transparent = pent->f_inbound_transparent;
      netProcessor.main_accept(NEW(new HttpAccept(pent->type)), pent->fd, NULL, NULL, false, false, opt);
    }
  } else {
    static HttpPortTypes type = SERVER_PORT_DEFAULT;
    char *attr_string = 0;
    opt.port = port;

    // function can be called several times : do memory allocation once
    
    REC_ReadConfigStringAlloc(attr_string, "proxy.config.http.server_port_attr");
    REC_ReadConfigInteger(opt.recv_bufsize, "proxy.config.net.sock_recv_buffer_size_in");
    REC_ReadConfigInteger(opt.send_bufsize, "proxy.config.net.sock_send_buffer_size_in");
    REC_ReadConfigInteger(opt.sockopt_flags, "proxy.config.net.sock_option_flag_in");

    if (attr_string) {
      HttpEntryPoint attr;
      get_connection_attributes(attr_string, &attr);
      type = attr.type;
      opt.domain = attr.domain;
      Debug("http_tproxy", "Primary listen socket transparency is %s\n",
        attr.f_inbound_transparent &&  attr.f_outbound_transparent ? "bidirectional"
        : attr.f_inbound_transparent ? "inbound"
        : attr.f_outbound_transparent ? "outbound"
        : "off"
      );
      opt.f_outbound_transparent = attr.f_outbound_transparent;
      opt.f_inbound_transparent = attr.f_inbound_transparent;
      ats_free(attr_string);
    }

    netProcessor.main_accept(NEW(new HttpAccept(type)), fd,  NULL, NULL, false, false, opt);

    http_other_port_array = parse_http_server_other_ports();
    if (http_other_port_array) {
      for (int i = 0; http_other_port_array[i].port != -1; i++) {
        HttpEntryPoint & e = http_other_port_array[i];
        if ((e.port<1) || (e.port> 65535))
          Warning("additional port out of range ignored: %d", e.port);
        else {
          opt.port = e.port;
          opt.domain = e.domain;
          opt.f_outbound_transparent = e.f_outbound_transparent;
          opt.f_inbound_transparent = e.f_inbound_transparent;
          netProcessor.main_accept(NEW(new HttpAccept(e.type)), e.fd, NULL, NULL, false, false, opt);
        }
      }
    }
  }

  SslConfigParams *sslParam = sslTerminationConfig.acquire();

  if (sslParam->getTerminationMode() & sslParam->SSL_TERM_MODE_CLIENT) {
    opt.reset();
    opt.port = sslParam->getAcceptPort();
    opt.accept_threads = accept_threads;
    sslNetProcessor.main_accept(NEW(new HttpAccept(SERVER_PORT_SSL)), ssl_fd, 0, 0, false, false, opt);
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

  opt.port = port;
  opt.accept_threads = accept_threads;
  
  // The backdoor only binds the loopback interface
  netProcessor.main_accept(NEW(new HttpAccept(SERVER_PORT_DEFAULT, true)), NO_FD, 0, 0, false, true, opt);
}
