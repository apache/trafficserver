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

#ifdef USE_NCA
#include "HttpNcaClient.h"
#endif

HttpPortEntry *http_port_attr_array = NULL;
HttpOtherPortEntry *http_other_port_array = NULL;


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

HttpPortTypes
get_connection_attributes(char *attr)
{
  HttpPortTypes attributes = SERVER_PORT_DEFAULT;
  if (attr) {
    if (strlen(attr) > 1) {
      Warning("too many port attributes: '%s'", attr);
    } else {
      switch (*attr) {
      case 'C':
        attributes = SERVER_PORT_COMPRESSED;
        break;
      case 'X':
        attributes = SERVER_PORT_DEFAULT;
        break;
      case 'T':
        attributes = SERVER_PORT_BLIND_TUNNEL;
        break;
      default:
        Warning("unknown port attribute '%s'", attr);
        break;
      }
    }
  }
  return attributes;
}


static HttpOtherPortEntry *
parse_http_server_other_ports()
{
  int list_entries;
  int accept_index = 0;
  int port = 0;
  char *other_ports_str = NULL;
  char *cur_entry;
  char *attr_str;
  Tokenizer listTok(", ");
  HttpOtherPortEntry *additional_ports_array;

  other_ports_str = HTTP_ConfigReadString("proxy.config.http.server_other_ports");

  if (!other_ports_str || *other_ports_str == '\0') {
    return NULL;
  }

  list_entries = listTok.Initialize(other_ports_str, SHARE_TOKS);

  if (list_entries > 0) {
    additional_ports_array = new HttpOtherPortEntry[list_entries + 1];
    additional_ports_array[0].port = -1;
  } else {
    return NULL;
  }

  for (int i = 0; i < list_entries; i++) {
    cur_entry = (char *) listTok[i];

    // Check to see if there is a port attribute
    attr_str = strchr(cur_entry, ':');
    if (attr_str != NULL) {
      *attr_str = '\0';
      attr_str = attr_str + 1;
    }
    // Port value
    // coverity[secure_coding]
    // sscanf of token from tokenizer
    if (sscanf(cur_entry, "%d", &port) != 1) {
      Warning("failed to read accept port, discarding");
      continue;
    }

    additional_ports_array[accept_index].port = port;
    additional_ports_array[accept_index].type = get_connection_attributes(attr_str);

    accept_index++;
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
  HttpConfig::startup();
  httpSessionManager.init();
  http_pages_init();
  ink_mutex_init(&debug_sm_list_mutex, "HttpSM Debug List");
  ink_mutex_init(&debug_cs_list_mutex, "HttpCS Debug List");
  // DI's request to disable/reenable ICP on the fly
  icp_dynamic_enabled = 1;
  // INKqa11918
  init_max_chunk_buf();

#ifndef INK_NO_API
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
start_HttpProxyServer(int fd, int port, int ssl_fd)
{
  char *dump_every_str = 0;
  static bool called_once = false;

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

/*
    char * state_machines_max_count = NULL;
    if ((state_machines_max_count =
         getenv("HTTP_STATE_MACHINE_MAX_COUNT")) != 0)
    {
        HttpStateMachine::m_state_machines_max_count =
            atoi(state_machines_max_count);

        HTTP_ASSERT (HttpStateMachine::m_state_machines_max_count >= 1);
    }
    */
  ///////////////////////////////////
  // start accepting connections   //
  ///////////////////////////////////
  int sock_recv_buffer_size_in = 0;
  int sock_send_buffer_size_in = 0;
  unsigned long sock_option_flag_in = 0;
  char *attr_string = 0;
  static HttpPortTypes attr = SERVER_PORT_DEFAULT;

  if (!called_once) {
    // function can be called several times : do memory allocation once
    REC_ReadConfigStringAlloc(attr_string, "proxy.config.http.server_port_attr");
    REC_ReadConfigInteger(sock_recv_buffer_size_in, "proxy.config.net.sock_recv_buffer_size_in");
    REC_ReadConfigInteger(sock_send_buffer_size_in, "proxy.config.net.sock_send_buffer_size_in");
    REC_ReadConfigInteger(sock_option_flag_in, "proxy.config.net.sock_option_flag_in");

    // deprecated configuration options - bcall 4/25/07
    // these should be removed in the future
    if (sock_recv_buffer_size_in == 0 && sock_send_buffer_size_in == 0 && sock_option_flag_in == 0) {
      REC_ReadConfigInteger(sock_recv_buffer_size_in, "proxy.config.net.sock_recv_buffer_size");
      REC_ReadConfigInteger(sock_send_buffer_size_in, "proxy.config.net.sock_send_buffer_size");
      REC_ReadConfigInteger(sock_option_flag_in, "proxy.config.net.sock_option_flag");
    }
    // end of deprecated config options

    if (attr_string) {
      attr = get_connection_attributes(attr_string);
      xfree(attr_string);
    }
    called_once = true;
    if (http_port_attr_array) {
      for (int i = 0; http_port_attr_array[i].fd != NO_FD; i++) {
        HttpPortEntry & e = http_port_attr_array[i];
        if (e.fd)
          netProcessor.main_accept(NEW(new HttpAccept(e.type)), e.fd, 0, NULL, NULL, false,
                                   sock_recv_buffer_size_in, sock_send_buffer_size_in, sock_option_flag_in);
      }
    } else {
      // If traffic_server wasn't started with -A, get the list
      // of other ports directly.
      http_other_port_array = parse_http_server_other_ports();
    }
  }
  if (!http_port_attr_array) {
    netProcessor.main_accept(NEW(new HttpAccept(attr)), fd, port, NULL, NULL, false,
                             sock_recv_buffer_size_in, sock_send_buffer_size_in, sock_option_flag_in);

    if (http_other_port_array) {
      for (int i = 0; http_other_port_array[i].port != -1; i++) {
        HttpOtherPortEntry & e = http_other_port_array[i];
        if ((e.port<1) || (e.port> 65535))
          Warning("additional port out of range ignored: %d", e.port);
        else
          netProcessor.main_accept(NEW(new HttpAccept(e.type)), fd, e.port, NULL, NULL, false,
                                   sock_recv_buffer_size_in, sock_send_buffer_size_in, sock_option_flag_in);
      }
    }
  } else {
    for (int i = 0; http_port_attr_array[i].fd != NO_FD; i++) {
      HttpPortEntry & e = http_port_attr_array[i];
      if (!e.fd) {
        netProcessor.main_accept(NEW(new HttpAccept(attr)), fd, port, NULL, NULL, false,
                                 sock_recv_buffer_size_in, sock_send_buffer_size_in, sock_option_flag_in);
      }
    }
  }
#ifdef HAVE_LIBSSL
  SslConfigParams *sslParam = sslTerminationConfig.acquire();

  if (sslParam->getTerminationMode() & sslParam->SSL_TERM_MODE_CLIENT)
    sslNetProcessor.main_accept(NEW(new HttpAccept(SERVER_PORT_SSL)), ssl_fd, sslParam->getAcceptPort());

  sslTerminationConfig.release(sslParam);
#endif

#ifdef USE_NCA
  start_NcaServer();
#endif

#ifdef DEBUG
  if (diags->on("http_dump")) {
//      HttpStateMachine::dump_state_machines();
  }
#endif
#ifndef INK_NO_TESTS
  if (is_action_tag_set("http_update_test")) {
    init_http_update_test();
  }
#endif
}

void
start_HttpProxyServerBackDoor(int port)
{
  netProcessor.main_accept(NEW(new HttpAccept(SERVER_PORT_DEFAULT, true)), NO_FD, port);
}
