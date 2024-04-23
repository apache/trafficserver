/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "iocore/net/quic/QUICApplication.h"
#include "iocore/net/quic/QUICStreamManager.h"
#include "iocore/net/quic/QUICEvents.h"
#include "iocore/net/quic/QUICStreamAdapter.h"
#include "iocore/net/quic/QUICStream.h"

class MockQUICContext;

using namespace std::literals;
std::string_view negotiated_application_name_sv = "h3-29"sv;

class MockQUICLDConfig : public QUICLDConfig
{
  uint32_t
  packet_threshold() const override
  {
    return 3;
  }

  float
  time_threshold() const override
  {
    return 1.25;
  }

  ink_hrtime
  granularity() const override
  {
    return HRTIME_MSECONDS(1);
  }

  ink_hrtime
  initial_rtt() const override
  {
    return HRTIME_MSECONDS(100);
  }
};

class MockQUICCCConfig : public QUICCCConfig
{
  uint32_t
  max_datagram_size() const
  {
    return 1200;
  }

  uint32_t
  initial_window() const override
  {
    return 10;
  }

  uint32_t
  minimum_window() const override
  {
    return 2;
  }

  float
  loss_reduction_factor() const override
  {
    return 0.5;
  }

  uint32_t
  persistent_congestion_threshold() const override
  {
    return 2;
  }
};

class MockQUICConnectionInfoProvider : public QUICConnectionInfoProvider
{
  QUICConnectionId
  connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  peer_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  original_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  first_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  retry_source_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  initial_source_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  const QUICFiveTuple
  five_tuple() const override
  {
    return QUICFiveTuple();
  }

  std::string_view
  cids() const override
  {
    using namespace std::literals;
    return std::string_view("00000000-00000000"sv);
  }

  uint32_t
  pmtu() const override
  {
    return 1280;
  }

  NetVConnectionContext_t
  direction() const override
  {
    return NET_VCONNECTION_OUT;
  }

  bool
  is_closed() const override
  {
    return false;
  }

  bool
  is_at_anti_amplification_limit() const override
  {
    return false;
  }

  bool
  is_address_validation_completed() const override
  {
    return true;
  }

  bool
  is_handshake_completed() const override
  {
    return true;
  }

  QUICVersion
  negotiated_version() const override
  {
    return QUIC_SUPPORTED_VERSIONS[0];
  }

  std::string_view
  negotiated_application_name() const override
  {
    return negotiated_application_name_sv;
  }
};

class MockQUICStreamManager : public QUICStreamManager
{
public:
  MockQUICStreamManager(QUICContext *context) : QUICStreamManager(context, nullptr) {}

  // for Test
  int
  getStreamFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::STREAM)];
  }

  int
  getAckFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::ACK)];
  }

  int
  getPingFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::PING)];
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

private:
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
};

class MockNetVConnection : public NetVConnection
{
public:
  MockNetVConnection(NetVConnectionContext_t context = NET_VCONNECTION_OUT) : NetVConnection() { netvc_context = context; }
  VIO *
  do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override
  {
    return nullptr;
  };
  VIO *
  do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override
  {
    return nullptr;
  };
  void do_io_close(int lerrno = -1) override {};
  void do_io_shutdown(ShutdownHowTo_t howto) override {};
  void reenable(VIO *vio) override {};
  void reenable_re(VIO *vio) override {};
  void set_active_timeout(ink_hrtime timeout_in) override {};
  void set_inactivity_timeout(ink_hrtime timeout_in) override {};
  void cancel_active_timeout() override {};
  void cancel_inactivity_timeout() override {};
  void add_to_keep_alive_queue() override {};
  void remove_from_keep_alive_queue() override {};
  bool
  add_to_active_queue() override
  {
    return true;
  };
  ink_hrtime
  get_active_timeout() override
  {
    return 0;
  }
  ink_hrtime
  get_inactivity_timeout() override
  {
    return 0;
  }
  void
  apply_options() override
  {
  }
  SOCKET
  get_socket() override { return 0; }
  int
  set_tcp_init_cwnd(int init_cwnd)
  {
    return 0;
  }
  int
  set_tcp_congestion_control(int side) override
  {
    return 0;
  }
  void set_local_addr() override {};
  void set_remote_addr() override {};

  NetVConnectionContext_t
  get_context() const
  {
    return netvc_context;
  }
};

class MockQUICConnection : public QUICConnection
{
public:
  MockQUICConnection(NetVConnectionContext_t context = NET_VCONNECTION_OUT) : QUICConnection(), _direction(context)
  {
    this->_mutex = new_ProxyMutex();
  };

  QUICConnectionId
  connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  peer_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  original_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  first_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  retry_source_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  QUICConnectionId
  initial_source_connection_id() const override
  {
    return {reinterpret_cast<const uint8_t *>("\x00"), 1};
  }

  const QUICFiveTuple
  five_tuple() const override
  {
    return QUICFiveTuple();
  }

  std::string_view
  cids() const override
  {
    using namespace std::literals;
    return std::string_view("00000000-00000000"sv);
  }

  uint32_t
  pmtu() const override
  {
    return 1280;
  }

  NetVConnectionContext_t
  direction() const override
  {
    return _direction;
  }

  void
  close_quic_connection(QUICConnectionErrorUPtr error) override
  {
  }

  void
  reset_quic_connection() override
  {
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

  QUICStreamManager *
  stream_manager() override
  {
    return nullptr;
  }

  bool
  is_closed() const override
  {
    return false;
  }

  bool
  is_at_anti_amplification_limit() const override
  {
    return false;
  }

  bool
  is_address_validation_completed() const override
  {
    return true;
  }

  bool
  is_handshake_completed() const override
  {
    return true;
  }

  void
  handle_received_packet(UDPPacket *) override
  {
  }

  void
  ping() override
  {
  }

  QUICVersion
  negotiated_version() const override
  {
    return QUIC_SUPPORTED_VERSIONS[0];
  }

  std::string_view
  negotiated_application_name() const override
  {
    return negotiated_application_name_sv;
  }

  int             _transmit_count   = 0;
  int             _retransmit_count = 0;
  Ptr<ProxyMutex> _mutex;
  int             _totalFrameCount = 0;
  int             _frameCount[256] = {0};

  QUICTransportParametersInEncryptedExtensions dummy_transport_parameters();
  NetVConnectionContext_t                      _direction;
};

class MockQUICContext : public QUICContext
{
public:
  MockQUICContext() : QUICContext() { _info = std::make_unique<MockQUICConnectionInfoProvider>(); }

  virtual QUICConnectionInfoProvider *
  connection_info() const override
  {
    return _info.get();
  }
  virtual QUICConfig::scoped_config
  config() const override
  {
    return _config;
  }

private:
  QUICConfig::scoped_config                   _config;
  std::unique_ptr<QUICConnectionInfoProvider> _info;
};

class MockQUICStreamAdapter : public QUICStreamAdapter
{
public:
  MockQUICStreamAdapter(QUICStream &stream) : QUICStreamAdapter(stream) {}

  void
  write_to_stream(const uint8_t *buf, size_t len)
  {
    this->_total_sending_data_len += len;
    this->_sending_data_len       += len;
  }

  int64_t
  write(QUICOffset offset, const uint8_t *data, uint64_t data_length, bool fin) override
  {
    this->_total_receiving_data_len += data_length;
    this->_receiving_data_len       += data_length;
    return data_length;
  }
  bool
  is_eos() override
  {
    return false;
  }
  uint64_t
  unread_len() override
  {
    return this->_sending_data_len;
  }
  uint64_t
  read_len() override
  {
    return 0;
  }
  uint64_t
  total_len() override
  {
    return this->_total_sending_data_len;
  }
  void
  encourge_read() override
  {
  }
  void
  encourge_write() override
  {
  }
  void
  notify_eos() override
  {
  }

protected:
  Ptr<IOBufferBlock>
  _read(size_t len) override
  {
    this->_sending_data_len  -= len;
    Ptr<IOBufferBlock> block  = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    block->alloc(iobuffer_size_to_index(len, BUFFER_SIZE_INDEX_32K));
    block->fill(len);
    return block;
  }

private:
  size_t _sending_data_len         = 0;
  size_t _total_sending_data_len   = 0;
  size_t _receiving_data_len       = 0;
  size_t _total_receiving_data_len = 0;
};

class MockQUICApplication : public QUICApplication
{
public:
  MockQUICApplication(QUICConnection *c) : QUICApplication(c) { SET_HANDLER(&MockQUICApplication::main_event_handler); }

  int
  main_event_handler(int event, Event *data)
  {
    if (event == 12345) {}
    return EVENT_CONT;
  }

  void
  on_stream_open(QUICStream &stream) override
  {
    auto               ite     = this->_streams.emplace(stream.id(), stream);
    QUICStreamAdapter &adapter = ite.first->second;
    stream.set_io_adapter(&adapter);
  }

  void
  on_stream_close(QUICStream &stream) override
  {
  }

  void
  send(const uint8_t *data, size_t size, QUICStreamId stream_id)
  {
    auto  ite     = this->_streams.find(stream_id);
    auto &adapter = ite->second;
    adapter.write_to_stream(data, size);

    // eventProcessor.schedule_imm(this, ET_CALL, 12345, adapter);
  }

  std::unordered_map<QUICStreamId, MockQUICStreamAdapter> _streams;
};

class MockContinuation : public Continuation
{
public:
  MockContinuation(Ptr<ProxyMutex> m) : Continuation(m) { SET_HANDLER(&MockContinuation::event_handler); }
  int
  event_handler(int event, Event *data)
  {
    return EVENT_CONT;
  }
};

class MockQUICRTTProvider : public QUICRTTProvider
{
  ink_hrtime
  latest_rtt() const override
  {
    return HRTIME_MSECONDS(1);
  }

  ink_hrtime
  rttvar() const override
  {
    return HRTIME_MSECONDS(1);
  }

  ink_hrtime
  smoothed_rtt() const override
  {
    return HRTIME_MSECONDS(1);
  }

  ink_hrtime
  congestion_period(uint32_t threshold) const override
  {
    return HRTIME_MSECONDS(1);
  }
};
