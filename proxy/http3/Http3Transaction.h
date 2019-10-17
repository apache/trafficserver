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

#include "I_VConnection.h"
#include "ProxyTransaction.h"
#include "Http3FrameDispatcher.h"
#include "Http3FrameCollector.h"

class QUICStreamIO;
class HQSession;
class Http09Session;
class Http3Session;
class Http3HeaderFramer;
class Http3DataFramer;

class HQTransaction : public ProxyTransaction
{
public:
  using super = ProxyTransaction;

  HQTransaction(HQSession *session, QUICStreamIO *stream_io);
  virtual ~HQTransaction();

  // Implement ProxyClienTransaction interface
  void set_active_timeout(ink_hrtime timeout_in) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  void cancel_inactivity_timeout() override;
  void transaction_done() override;
  bool allow_half_open() const override;
  void destroy() override;
  void release(IOBufferReader *r) override;
  int get_transaction_id() const override;
  void increment_client_transactions_stat() override;
  void decrement_client_transactions_stat() override;

  // VConnection interface
  virtual VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  virtual VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0,
                           bool owner = false) override;
  virtual void do_io_close(int lerrno = -1) override;
  virtual void do_io_shutdown(ShutdownHowTo_t) override;
  virtual void reenable(VIO *) override;

  // HQTransaction
  virtual int state_stream_open(int, void *)             = 0;
  virtual int state_stream_closed(int event, void *data) = 0;
  NetVConnectionContext_t direction() const;

protected:
  virtual int64_t _process_read_vio()  = 0;
  virtual int64_t _process_write_vio() = 0;
  Event *_send_tracked_event(Event *, int, VIO *);
  void _signal_read_event();
  void _signal_write_event();

  EThread *_thread           = nullptr;
  Event *_cross_thread_event = nullptr;

  MIOBuffer _read_vio_buf  = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;
  QUICStreamIO *_stream_io = nullptr;

  VIO _read_vio;
  VIO _write_vio;
  Event *_read_event  = nullptr;
  Event *_write_event = nullptr;

  HTTPHdr _header; ///< HTTP header buffer for decoding
};

class Http3Transaction : public HQTransaction
{
public:
  using super = HQTransaction;

  Http3Transaction(Http3Session *session, QUICStreamIO *stream_io);
  ~Http3Transaction();

  int state_stream_open(int event, void *data) override;
  int state_stream_closed(int event, void *data) override;

  void do_io_close(int lerrno = -1) override;

  bool is_response_header_sent() const;
  bool is_response_body_sent() const;

private:
  int64_t _process_read_vio() override;
  int64_t _process_write_vio() override;

  ParseResult _convert_header_from_3_to_1_1(HTTPHdr *hdr);
  int _on_qpack_decode_complete();

  // These are for HTTP/3
  Http3FrameDispatcher _frame_dispatcher;
  Http3FrameCollector _frame_collector;
  Http3FrameGenerator *_header_framer = nullptr;
  Http3FrameGenerator *_data_framer   = nullptr;
  Http3FrameHandler *_header_handler  = nullptr;
  Http3FrameHandler *_data_handler    = nullptr;
};

/**
   Only for interop. Will be removed.
 */
class Http09Transaction : public HQTransaction
{
public:
  using super = HQTransaction;

  Http09Transaction(Http09Session *session, QUICStreamIO *stream_io);
  ~Http09Transaction();

  int state_stream_open(int event, void *data) override;
  int state_stream_closed(int event, void *data) override;

  void do_io_close(int lerrno = -1) override;

private:
  int64_t _process_read_vio() override;
  int64_t _process_write_vio() override;

  // These are for HTTP/0.9
  bool _protocol_detected          = false;
  bool _legacy_request             = false;
  bool _client_req_header_complete = false;
};
