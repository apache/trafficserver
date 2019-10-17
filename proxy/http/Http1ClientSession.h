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

#pragma once

#include "P_Net.h"
#include "InkAPIInternal.h"
#include "HTTP.h"
#include "HttpConfig.h"
#include "IPAllow.h"
#include "ProxySession.h"
#include "Http1Transaction.h"

#ifdef USE_HTTP_DEBUG_LISTS
extern ink_mutex debug_cs_list_mutex;
#endif

class HttpSM;
class Http1ServerSession;

class Http1ClientSession : public ProxySession
{
public:
  typedef ProxySession super; ///< Parent type.
  Http1ClientSession();

  // Implement ProxySession interface.
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader) override;
  void start() override;
  void release(ProxyTransaction *trans) override; // Indicate we are done with a transaction
  void release_transaction();
  void destroy() override;
  void free() override;

  void attach_server_session(Http1ServerSession *ssession, bool transaction_done = true) override;

  // Implement VConnection interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = nullptr,
                   bool owner = false) override;

  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  // Accessor Methods
  bool allow_half_open() const;
  void set_half_close_flag(bool flag) override;
  bool get_half_close_flag() const override;
  bool is_chunked_encoding_supported() const override;
  NetVConnection *get_netvc() const override;
  int get_transact_count() const override;
  virtual bool is_outbound_transparent() const;

  Http1ServerSession *get_server_session() const override;
  void set_active_timeout(ink_hrtime timeout_in) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  void cancel_inactivity_timeout() override;
  const char *get_protocol_string() const override;

  void increment_current_active_client_connections_stat() override;
  void decrement_current_active_client_connections_stat() override;

private:
  Http1ClientSession(Http1ClientSession &);

  void new_transaction();

  int state_keep_alive(int event, void *data);
  int state_slave_keep_alive(int event, void *data);
  int state_wait_for_close(int event, void *data);

  enum C_Read_State {
    HCS_INIT,
    HCS_ACTIVE_READER,
    HCS_KEEP_ALIVE,
    HCS_HALF_CLOSED,
    HCS_CLOSED,
  };

  NetVConnection *client_vc = nullptr;
  int magic                 = HTTP_SS_MAGIC_DEAD;
  int transact_count        = 0;
  bool half_close           = false;
  bool conn_decrease        = false;

  MIOBuffer *read_buffer  = nullptr;
  IOBufferReader *_reader = nullptr;

  C_Read_State read_state = HCS_INIT;

  VIO *ka_vio       = nullptr;
  VIO *slave_ka_vio = nullptr;

  Http1ServerSession *bound_ss = nullptr;

  int released_transactions = 0;

public:
  // Link<Http1ClientSession> debug_link;
  LINK(Http1ClientSession, debug_link);

  /// Set outbound connection to transparent.
  bool f_outbound_transparent = false;

  Http1Transaction trans;
};

extern ClassAllocator<Http1ClientSession> http1ClientSessionAllocator;
