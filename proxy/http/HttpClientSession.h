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

/****************************************************************************

   HttpClientSession.h

   Description:

 ****************************************************************************/

#ifndef _HTTP_CLIENT_SESSION_H_
#define _HTTP_CLIENT_SESSION_H_

#include "libts.h"
#include "P_Net.h"
#include "InkAPIInternal.h"
#include "HTTP.h"
#include "HttpConfig.h"

extern ink_mutex debug_cs_list_mutex;

class HttpSM;
class HttpServerSession;

class SecurityContext;

class HttpClientSession: public VConnection
{
public:
  HttpClientSession();
  void cleanup();
  virtual void destroy();

  static HttpClientSession *allocate();

  void new_connection(NetVConnection * new_vc, bool backdoor = false);

  virtual VIO *do_io_read(Continuation * c, int64_t nbytes = INT64_MAX, MIOBuffer * buf = 0);
  virtual VIO *do_io_write(Continuation * c = NULL, int64_t nbytes = INT64_MAX, IOBufferReader * buf = 0, bool owner = false);

  virtual void do_io_close(int lerrno = -1);
  virtual void do_io_shutdown(ShutdownHowTo_t howto);
  virtual void reenable(VIO * vio);

  void set_half_close_flag() { half_close = true; };
  virtual void release(IOBufferReader * r);
  NetVConnection *get_netvc() const { return client_vc;  };

  virtual void attach_server_session(HttpServerSession * ssession, bool transaction_done = true);
  HttpServerSession *get_server_session() const { return bound_ss; };

  // Used for the cache authenticated HTTP content feature
  HttpServerSession *get_bound_ss();

  // Functions for manipulating api hooks
  void ssn_hook_append(TSHttpHookID id, INKContInternal * cont);
  void ssn_hook_prepend(TSHttpHookID id, INKContInternal * cont);
  APIHook *ssn_hook_get(TSHttpHookID id);

  // Used to verify we are recording the current
  //   client transaction stat properly
  int64_t con_id;

  int get_transact_count() const { return  transact_count; }

  void* get_user_arg(int ix) const { return user_args[ix]; }
  void set_user_arg(int ix, void* arg) { user_args[ix] = arg; }

private:
  HttpClientSession(HttpClientSession &);

  int state_keep_alive(int event, void *data);
  int state_slave_keep_alive(int event, void *data);
  int state_wait_for_close(int event, void *data);
  void set_tcp_init_cwnd();

  void handle_api_return(int event);
  int state_api_callout(int event, void *data);
  void do_api_callout(TSHttpHookID id);

  virtual void new_transaction();

  enum C_Read_State
  {
    HCS_INIT,
    HCS_ACTIVE_READER,
    HCS_KEEP_ALIVE,
    HCS_HALF_CLOSED,
    HCS_CLOSED
  };

  NetVConnection *client_vc;
  int magic;
  bool tcp_init_cwnd_set;
  int transact_count;
  bool half_close;
  bool conn_decrease;
  void *user_args[HTTP_SSN_TXN_MAX_USER_ARG];

  HttpServerSession *bound_ss;

  MIOBuffer *read_buffer;
  IOBufferReader *sm_reader;
  HttpSM *current_reader;
  C_Read_State read_state;

  VIO *ka_vio;
  VIO *slave_ka_vio;

  Link<HttpClientSession> debug_link;

  TSHttpHookID cur_hook_id;
  APIHook *cur_hook;
  int cur_hooks;

  // api_hooks must not be changed directly
  //  Use ssn_hook_{ap,pre}pend so hooks_set is
  //  updated
  HttpAPIHooks api_hooks;

public:
  bool backdoor_connect;
  int hooks_set;
  /// Local address for outbound connection.
  IpAddr outbound_ip4;
  /// Local address for outbound connection.
  IpAddr outbound_ip6;
  /// Local port for outbound connection.
  uint16_t outbound_port;
  /// Set outbound connection to transparent.
  bool f_outbound_transparent;
  /// Transparently pass-through non-HTTP traffic.
  bool f_transparent_passthrough;
  /// DNS resolution preferences.
  HostResStyle host_res_style;
  /// acl method mask - cache IpAllow::match() call
  uint32_t acl_method_mask;

  // for DI. An active connection is one that a request has
  // been successfully parsed (PARSE_DONE) and it remains to
  // be active until the transaction goes through or the client
  // aborts.
  bool m_active;
  // Session specific debug flag
  bool debug_on;
};

inline APIHook *
HttpClientSession::ssn_hook_get(TSHttpHookID id)
{
  return api_hooks.get(id);
}

extern ClassAllocator<HttpClientSession> httpClientSessionAllocator;

#endif
