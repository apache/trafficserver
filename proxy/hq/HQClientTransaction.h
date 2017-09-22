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

#include "ProxyClientTransaction.h"

class QUICStreamIO;
class HQClientSession;

class HQClientTransaction : public ProxyClientTransaction
{
public:
  using super = ProxyClientTransaction;

  HQClientTransaction(HQClientSession *session, QUICStreamIO *stream_io);

  // Implement VConnection interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;

  // Implement ProxyClienTransaction interface
  void set_active_timeout(ink_hrtime timeout_in) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  void cancel_inactivity_timeout() override;
  void transaction_done() override;
  bool allow_half_open() const override;
  void destroy() override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;
  void release(IOBufferReader *r) override;

  // HQClientTransaction specific methods
  int main_event_handler(int event, void *edata);

private:
  void _read_request();
  void _write_response();

  MIOBuffer _read_vio_buf = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;
  VIO _read_vio;
  VIO _write_vio;
  QUICStreamIO *_stream_io = nullptr;
  Event *_read_event       = nullptr;
  Event *_write_event      = nullptr;
};
