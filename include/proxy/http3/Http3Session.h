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

#pragma once

#include "proxy/ProxySession.h"
#include "proxy/http3/Http3Transaction.h"
#include "proxy/http3/Http3FrameCounter.h"
#include "proxy/http3/QPACK.h"

class HQSession : public ProxySession
{
public:
  using super = ProxySession; ///< Parent type

  HQSession(NetVConnection *vc);
  virtual ~HQSession();

  // Implement VConnection interface
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  // Implement ProxySession interface
  const char *get_protocol_string() const override;
  int         populate_protocol(std::string_view *result, int size) const override;
  void        new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader) override;
  void        start() override;
  void        destroy() override;
  void        free() override;
  void        release(ProxyTransaction *trans) override;
  int         get_transact_count() const override;

  // HQSession
  void           add_transaction(HQTransaction *trans);
  void           remove_transaction(HQTransaction *trans);
  HQTransaction *get_transaction(QUICStreamId);

private:
  // this should be unordered map?
  Queue<HQTransaction> _transaction_list;

  char _protocol_string[16];

  int main_event_handler(int, void *);
};

class Http3Session : public HQSession
{
public:
  using super = HQSession; ///< Parent type

  Http3Session(NetVConnection *vc);
  ~Http3Session();

  // ProxySession interface
  HTTPVersion get_version(HTTPHdr &hdr) const override;
  void        increment_current_active_connections_stat() override;
  void        decrement_current_active_connections_stat() override;
  bool        is_protocol_framed() const override;
  uint64_t    get_received_frame_count(uint64_t type) const override;

  // Implement ProxySession interface
  const char *get_protocol_string() const override;

  QPACK             *local_qpack();
  QPACK             *remote_qpack();
  Http3FrameCounter *get_received_frame_counter();

private:
  QPACK            *_remote_qpack = nullptr; // QPACK for decoding
  QPACK            *_local_qpack  = nullptr; // QPACK for encoding
  Http3FrameCounter _received_frame_counter;
};

/**
   Only for interop. Will be removed.
 */
class Http09Session : public HQSession
{
public:
  using super = HQSession; ///< Parent type

  Http09Session(NetVConnection *vc) : HQSession(vc) {}
  ~Http09Session();

  // ProxySession interface
  HTTPVersion get_version(HTTPHdr &hdr) const override;
  void        increment_current_active_connections_stat() override;
  void        decrement_current_active_connections_stat() override;

private:
};
