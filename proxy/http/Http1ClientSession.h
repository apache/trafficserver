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

   Http1ClientSession.h

   Description:

 ****************************************************************************/

#ifndef _HTTP1_CLIENT_SESSION_H_
#define _HTTP1_CLIENT_SESSION_H_

//#include "libts.h"
#include "P_Net.h"
#include "InkAPIInternal.h"
#include "HTTP.h"
#include "HttpConfig.h"
#include "IPAllow.h"
#include "ProxyClientSession.h"
#include "Http1ClientTransaction.h"

#ifdef USE_HTTP_DEBUG_LISTS
extern ink_mutex debug_cs_list_mutex;
#endif

class HttpSM;
class HttpServerSession;

class Http1ClientSession : public ProxyClientSession
{
public:
  typedef ProxyClientSession super; ///< Parent type.
  Http1ClientSession();

  // Implement ProxyClientSession interface.
  void destroy() override;
  void free() override;
  void release_transaction();

  void
  start() override
  {
    // Troll for data to get a new transaction
    this->release(&trans);
  }

  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor) override;

  // Implement VConnection interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  VIO *do_io_write(Continuation *c = NULL, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;

  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  void
  set_half_close_flag(bool flag) override
  {
    half_close = flag;
  }

  bool
  get_half_close_flag() const override
  {
    return half_close;
  }

  bool
  is_chunked_encoding_supported() const override
  {
    return true;
  }

  NetVConnection *
  get_netvc() const override
  {
    return client_vc;
  }

  void
  release_netvc() override
  {
    // Make sure the vio's are also released to avoid
    // later surprises in inactivity timeout
    if (client_vc) {
      client_vc->do_io_read(NULL, 0, NULL);
      client_vc->do_io_write(NULL, 0, NULL);
      client_vc->set_action(NULL);
      client_vc = NULL;
    }
  }

  int
  get_transact_count() const override
  {
    return transact_count;
  }

  virtual bool
  is_outbound_transparent() const
  {
    return f_outbound_transparent;
  }

  // Indicate we are done with a transaction
  void release(ProxyClientTransaction *trans) override;

  virtual uint16_t
  get_outbound_port() const
  {
    return outbound_port;
  }

  virtual IpAddr
  get_outbound_ip4() const
  {
    return outbound_ip4;
  }

  virtual IpAddr
  get_outbound_ip6() const
  {
    return outbound_ip6;
  }

  void attach_server_session(HttpServerSession *ssession, bool transaction_done = true) override;

  HttpServerSession *
  get_server_session() const override
  {
    return bound_ss;
  }

  void
  set_active_timeout(ink_hrtime timeout_in) override
  {
    if (client_vc)
      client_vc->set_active_timeout(timeout_in);
  }

  void
  set_inactivity_timeout(ink_hrtime timeout_in) override
  {
    if (client_vc)
      client_vc->set_inactivity_timeout(timeout_in);
  }

  void
  cancel_inactivity_timeout() override
  {
    if (client_vc)
      client_vc->cancel_inactivity_timeout();
  }

  const char *
  get_protocol_string() const override
  {
    return "http";
  }

  bool
  is_transparent_passthrough_allowed() const override
  {
    return f_transparent_passthrough;
  }

private:
  Http1ClientSession(Http1ClientSession &);

  void new_transaction();

  int state_keep_alive(int event, void *data);
  int state_slave_keep_alive(int event, void *data);
  int state_wait_for_close(int event, void *data);
  void set_tcp_init_cwnd();

  enum C_Read_State {
    HCS_INIT,
    HCS_ACTIVE_READER,
    HCS_KEEP_ALIVE,
    HCS_HALF_CLOSED,
    HCS_CLOSED,
  };

  NetVConnection *client_vc;
  int magic;
  int transact_count;
  bool tcp_init_cwnd_set;
  bool half_close;
  bool conn_decrease;

  MIOBuffer *read_buffer;
  IOBufferReader *sm_reader;

  C_Read_State read_state;

  VIO *ka_vio;
  VIO *slave_ka_vio;

  HttpServerSession *bound_ss;

  int released_transactions;

public:
  // Link<Http1ClientSession> debug_link;
  LINK(Http1ClientSession, debug_link);

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

  Http1ClientTransaction trans;
};

extern ClassAllocator<Http1ClientSession> http1ClientSessionAllocator;

#endif
