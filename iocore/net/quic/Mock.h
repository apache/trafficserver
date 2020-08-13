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

#include "P_Net.h"

#include "QUICApplication.h"
#include "QUICStreamManager.h"
#include "QUICLossDetector.h"
#include "QUICPacketProtectionKeyInfo.h"
#include "QUICPinger.h"
#include "QUICPadder.h"
#include "QUICEvents.h"
#include "QUICPacketProtectionKeyInfo.h"
#include "QUICPinger.h"
#include "QUICPadder.h"
#include "QUICHandshakeProtocol.h"

class MockQUICContext;

using namespace std::literals;
std::string_view negotiated_application_name_sv = "h3-29"sv;

class MockQUICLDConfig : public QUICLDConfig
{
  uint32_t
  packet_threshold() const
  {
    return 3;
  }

  float
  time_threshold() const
  {
    return 1.25;
  }

  ink_hrtime
  granularity() const
  {
    return HRTIME_MSECONDS(1);
  }

  ink_hrtime
  initial_rtt() const
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
  initial_window() const
  {
    return 10;
  }

  uint32_t
  minimum_window() const
  {
    return 2;
  }

  float
  loss_reduction_factor() const
  {
    return 0.5;
  }

  uint32_t
  persistent_congestion_threshold() const
  {
    return 2;
  }
};

class MockQUICPathManager : public QUICPathManager
{
public:
  virtual ~MockQUICPathManager() {}
  virtual const QUICPath &
  get_current_path()
  {
    return _path;
  }
  virtual const QUICPath &
  get_verified_path()
  {
    return _path;
  }
  virtual void
  open_new_path(const QUICPath &path, ink_hrtime timeout_in)
  {
    return;
  }
  virtual void
  set_trusted_path(const QUICPath &path)
  {
    return;
  }

private:
  QUICPath _path = {{}, {}};
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

  int
  select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                       unsigned inlen) const override
  {
    return SSL_TLSEXT_ERR_OK;
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

  bool
  has_keys_for(QUICPacketNumberSpace space) const override
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

  // Override
  virtual QUICConnectionErrorUPtr
  handle_frame(QUICEncryptionLevel level, const QUICFrame &f) override
  {
    ++_frameCount[static_cast<int>(f.type())];
    ++_totalFrameCount;

    return nullptr;
  }

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
  do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
  {
    return nullptr;
  };
  VIO *
  do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false)
  {
    return nullptr;
  };
  void do_io_close(int lerrno = -1){};
  void do_io_shutdown(ShutdownHowTo_t howto){};
  void reenable(VIO *vio){};
  void reenable_re(VIO *vio){};
  void set_active_timeout(ink_hrtime timeout_in){};
  void set_inactivity_timeout(ink_hrtime timeout_in){};
  void cancel_active_timeout(){};
  void cancel_inactivity_timeout(){};
  void add_to_keep_alive_queue(){};
  void remove_from_keep_alive_queue(){};
  bool
  add_to_active_queue()
  {
    return true;
  };
  ink_hrtime
  get_active_timeout()
  {
    return 0;
  }
  ink_hrtime
  get_inactivity_timeout()
  {
    return 0;
  }
  void
  apply_options()
  {
  }
  SOCKET
  get_socket() { return 0; }
  int
  set_tcp_init_cwnd(int init_cwnd)
  {
    return 0;
  }
  int
  set_tcp_congestion_control(int side)
  {
    return 0;
  }
  void set_local_addr(){};
  void set_remote_addr(){};

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

  std::vector<QUICFrameType>
  interests() override
  {
    return {QUICFrameType::CONNECTION_CLOSE};
  }

  QUICConnectionErrorUPtr
  handle_frame(QUICEncryptionLevel level, const QUICFrame &f) override
  {
    ++_frameCount[static_cast<int>(f.type())];
    ++_totalFrameCount;

    return nullptr;
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

  bool
  has_keys_for(QUICPacketNumberSpace space) const override
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

  int
  select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                       unsigned inlen) const override
  {
    return SSL_TLSEXT_ERR_OK;
  }

  int _transmit_count   = 0;
  int _retransmit_count = 0;
  Ptr<ProxyMutex> _mutex;
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};

  QUICTransportParametersInEncryptedExtensions dummy_transport_parameters();
  NetVConnectionContext_t _direction;
};

class MockQUICCongestionController : public QUICCongestionController
{
public:
  MockQUICCongestionController() {}
  // Override
  virtual void
  on_packets_lost(const std::map<QUICPacketNumber, QUICSentPacketInfoUPtr> &packets) override
  {
    for (auto &p : packets) {
      lost_packets.insert(p.first);
    }
  }

  virtual void
  on_packet_sent(size_t bytes_sent) override
  {
  }
  virtual void
  on_packets_acked(const std::vector<QUICSentPacketInfoUPtr> &packets) override
  {
  }
  void
  on_packet_number_space_discarded(size_t bytes_in_flight) override
  {
  }
  virtual void
  process_ecn(const QUICAckFrame &ack_frame, QUICPacketNumberSpace pn_space, ink_hrtime largest_acked_time_sent) override
  {
  }
  virtual void
  add_extra_credit() override
  {
  }
  virtual void
  reset() override
  {
  }
  virtual uint32_t
  credit() const override
  {
    return 0;
  }
  virtual uint32_t
  bytes_in_flight() const override
  {
    return 0;
  }
  virtual uint32_t
  congestion_window() const override
  {
    return 0;
  }
  virtual uint32_t
  current_ssthresh() const override
  {
    return 0;
  }

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

  std::set<QUICPacketNumber> lost_packets;

private:
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
};

class MockQUICPacketProtectionKeyInfo : public QUICPacketProtectionKeyInfo
{
public:
  const EVP_CIPHER *
  get_cipher(QUICKeyPhase phase) const override
  {
    return EVP_aes_128_gcm();
  }

  size_t
  get_tag_len(QUICKeyPhase phase) const override
  {
    return EVP_GCM_TLS_TAG_LEN;
  }

  const size_t *encryption_iv_len(QUICKeyPhase) const override
  {
    static size_t dummy = 12;
    return &dummy;
  }
};

class MockQUICContext : public QUICContext
{
public:
  MockQUICContext() : QUICContext()
  {
    _info         = std::make_unique<MockQUICConnectionInfoProvider>();
    _key_info     = std::make_unique<MockQUICPacketProtectionKeyInfo>();
    _path_manager = std::make_unique<MockQUICPathManager>();
    _ld_config    = std::make_unique<MockQUICLDConfig>();
    _cc_config    = std::make_unique<MockQUICCCConfig>();
  }

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
  virtual QUICRTTProvider *
  rtt_provider() const override
  {
    return const_cast<QUICRTTMeasure *>(&_rtt_measure);
  }

  virtual QUICPacketProtectionKeyInfo *
  key_info() const override
  {
    return _key_info.get();
  }

  virtual QUICLDConfig &
  ld_config() const override
  {
    return *_ld_config;
  }

  virtual QUICCCConfig &
  cc_config() const override
  {
    return *_cc_config;
  }

  virtual QUICPathManager *
  path_manager() const override
  {
    return _path_manager.get();
  }

private:
  QUICConfig::scoped_config _config;
  QUICRTTMeasure _rtt_measure;
  std::unique_ptr<QUICConnectionInfoProvider> _info;
  std::unique_ptr<QUICPacketProtectionKeyInfo> _key_info;
  std::unique_ptr<QUICLDConfig> _ld_config;
  std::unique_ptr<QUICCCConfig> _cc_config;
  std::unique_ptr<MockQUICPathManager> _path_manager;
};

class MockQUICLossDetector : public QUICLossDetector
{
public:
  MockQUICLossDetector(MockQUICContext &context)
    : QUICLossDetector(context, &_cc, &_rtt_measure, &this->_pinger, &this->_padder),
      _padder(NetVConnectionContext_t::NET_VCONNECTION_UNSET)
  {
  }
  void rcv_frame(std::shared_ptr<const QUICFrame>) {}

  void
  on_packet_sent(QUICPacketUPtr packet)
  {
  }

  void
  on_packet_number_space_discarded(QUICPacketNumberSpace pn_space)
  {
    this->_cc.on_packet_number_space_discarded(0);
  }

private:
  QUICPinger _pinger;
  QUICPadder _padder;
  QUICRTTMeasure _rtt_measure;
  MockQUICCongestionController _cc;
};

class MockQUICApplication : public QUICApplication
{
public:
  MockQUICApplication(QUICConnection *c) : QUICApplication(c) { SET_HANDLER(&MockQUICApplication::main_event_handler); }

  int
  main_event_handler(int event, Event *data)
  {
    if (event == 12345) {
      QUICStreamIO *stream_io = static_cast<QUICStreamIO *>(data->cookie);
      stream_io->write_reenable();
    }
    return EVENT_CONT;
  }

  void
  send(const uint8_t *data, size_t size, QUICStreamId stream_id)
  {
    QUICStreamIO *stream_io = this->_find_stream_io(stream_id);
    stream_io->write(data, size);

    eventProcessor.schedule_imm(this, ET_CALL, 12345, stream_io);
  }
};

class MockQUICPacketR : public QUICPacketR
{
public:
  MockQUICPacketR() : QUICPacketR(nullptr, {}, {}) {}

  QUICPacketType
  type() const override
  {
    return QUICPacketType::PROTECTED;
  }

  QUICConnectionId
  destination_cid() const override
  {
    return QUICConnectionId::ZERO();
  }

  QUICPacketNumber
  packet_number() const override
  {
    return 0;
  }

  const IpEndpoint &
  from() const override
  {
    return this->_from;
  }

  const IpEndpoint &
  to() const override
  {
    return this->_to;
  }

  void
  set_to(const IpEndpoint ep)
  {
    this->_to = ep;
  }

  void
  set_from(const IpEndpoint ep)
  {
    this->_from = ep;
  }

private:
  IpEndpoint _to;
  IpEndpoint _from;
};

class MockQUICHandshakeProtocol : public QUICHandshakeProtocol
{
public:
  MockQUICHandshakeProtocol(QUICPacketProtectionKeyInfo &pp_key_info) : QUICHandshakeProtocol(pp_key_info) {}

  int
  handshake(QUICHandshakeMsgs **out, const QUICHandshakeMsgs *in) override
  {
    return true;
  }

  void
  reset() override
  {
  }

  bool
  is_handshake_finished() const override
  {
    return true;
  }

  bool
  is_ready_to_derive() const override
  {
    return true;
  }

  int
  initialize_key_materials(QUICConnectionId cid, QUICVersion version) override
  {
    return 0;
  }

  const char *
  negotiated_cipher_suite() const override
  {
    return nullptr;
  }

  void
  negotiated_application_name(const uint8_t **name, unsigned int *len) const override
  {
    *name = reinterpret_cast<const uint8_t *>("h3");
    *len  = 2;
  }

  QUICEncryptionLevel
  current_encryption_level() const override
  {
    return QUICEncryptionLevel::INITIAL;
  }

  void
  abort_handshake() override
  {
    return;
  }

  std::shared_ptr<const QUICTransportParameters>
  local_transport_parameters() override
  {
    return nullptr;
  }

  std::shared_ptr<const QUICTransportParameters>
  remote_transport_parameters() override
  {
    return nullptr;
  }

  void
  set_local_transport_parameters(std::shared_ptr<const QUICTransportParameters> tp) override
  {
  }

  void
  set_remote_transport_parameters(std::shared_ptr<const QUICTransportParameters> tp) override
  {
  }
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

class MockQUICTransferProgressProvider : public QUICTransferProgressProvider
{
public:
  bool
  is_transfer_goal_set() const override
  {
    return false;
  }

  bool
  is_transfer_complete() const override
  {
    return this->_is_transfer_complete || this->_transfer_progress >= this->_transfer_goal;
  }

  bool
  is_cancelled() const override
  {
    return this->_is_reset_complete;
  }

  uint64_t
  transfer_progress() const override
  {
    return this->_transfer_progress;
  }

  uint64_t
  transfer_goal() const override
  {
    return this->_transfer_goal;
  }

  void
  set_transfer_complete(bool b)
  {
    this->_is_transfer_complete = b;
  }

  void
  set_cancelled(bool b)
  {
    this->_is_reset_complete = b;
  }

  void
  set_transfer_progress(uint64_t v)
  {
    this->_transfer_progress = v;
  }

  void
  set_transfer_goal(uint64_t v)
  {
    this->_transfer_goal = v;
  }

private:
  bool _is_transfer_complete  = false;
  bool _is_reset_complete     = false;
  uint64_t _transfer_progress = 0;
  uint64_t _transfer_goal     = UINT64_MAX;
};

class MockQUICFrameGenerator : public QUICFrameGenerator
{
public:
  bool
  will_generate_frame(QUICEncryptionLevel level, size_t connection_credit, bool ack_eliciting, uint32_t seq_num) override
  {
    return true;
  }

  QUICFrame *
  generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                 size_t current_packet_size, uint32_t seq_num) override
  {
    QUICFrame *frame              = QUICFrameFactory::create_ping_frame(buf, 0, this);
    QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
    this->_records_frame(0, std::move(info));
    return frame;
  }

  int lost_frame_count = 0;

private:
  void
  _on_frame_lost(QUICFrameInformationUPtr &info) override
  {
    lost_frame_count++;
  }
};
