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

   Http1ServerSession.h

   Description:


 ****************************************************************************/

#pragma once

#include "P_Net.h"

#include "HttpConnectionCount.h"
#include "HttpProxyAPIEnums.h"
#include "PoolableSession.h"
#include "Http1ServerTransaction.h"

class HttpSM;
class MIOBuffer;
class IOBufferReader;

enum {
  HTTP_SS_MAGIC_ALIVE = 0x0123FEED,
  HTTP_SS_MAGIC_DEAD  = 0xDEADFEED,
};

class Http1ServerSession : public PoolableSession
{
  using self_type  = Http1ServerSession;
  using super_type = PoolableSession;

public:
  Http1ServerSession();
  Http1ServerSession(self_type const &) = delete;
  self_type &operator=(self_type const &) = delete;
  ~Http1ServerSession()                   = default;

  ////////////////////
  // Methods
  void release(ProxyTransaction *) override;
  void destroy() override;
  void release_transaction();

  // VConnection Methods
  void do_io_close(int lerrno = -1) override;

  // ProxySession Methods
  int get_transact_count() const override;
  const char *get_protocol_string() const override;
  void increment_current_active_connections_stat() override;
  void decrement_current_active_connections_stat() override;
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader) override;
  void start() override;
  void free() override;
  bool is_chunked_encoding_supported() const override;

  IOBufferReader *get_remote_reader() override;
  IpEndpoint const &get_server_ip() const;

  ProxyTransaction *new_transaction() override;

  ////////////////////
  // Variables

  int transact_count = 0;

  // The ServerSession owns the following buffer which use
  //   for parsing the headers.  The server session needs to
  //   own the buffer so we can go from a keep-alive state
  //   to being acquired and parsing the header without
  //   changing the buffer we are doing I/O on.  We can
  //   not change the buffer for I/O without issuing a
  //   an asynchronous cancel on NT
  MIOBuffer *read_buffer = nullptr;

private:
  int magic = HTTP_SS_MAGIC_DEAD;

  IOBufferReader *_reader = nullptr;

  int released_transactions = 0;

  Http1ServerTransaction trans;
};

extern ClassAllocator<Http1ServerSession, true> httpServerSessionAllocator;

////////////////////////////////////////////
// INLINE

inline IOBufferReader *
Http1ServerSession::get_remote_reader()
{
  return _reader;
};
