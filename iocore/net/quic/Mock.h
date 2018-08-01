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
#include "QUICEvents.h"
#include "QUICPacketTransmitter.h"

class MockQUICStreamManager : public QUICStreamManager
{
public:
  MockQUICStreamManager() : QUICStreamManager() {}
  // Override
  virtual QUICErrorUPtr
  handle_frame(QUICEncryptionLevel level, std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;

    return QUICErrorUPtr(new QUICNoError());
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
  transmit_packet(QUICPacketUPtr packet) override
  {
    ++_transmit_count;
    return 1;
  }

  void
  retransmit_packet(const QUICPacket &packet) override
  {
    ++_retransmit_count;
  }

  Ptr<ProxyMutex>
  get_packet_transmitter_mutex() override
  {
    return this->_mutex;
  }

  std::vector<QUICFrameType>
  interests() override
  {
    return {QUICFrameType::CONNECTION_CLOSE};
  }

  QUICErrorUPtr
  handle_frame(QUICEncryptionLevel level, std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;

    return QUICErrorUPtr(new QUICNoError());
  }

  uint32_t
  minimum_quic_packet_size() override
  {
    return 1200;
  }

  uint32_t
  maximum_quic_packet_size() const override
  {
    return 1200;
  }

  uint32_t
  pmtu() const override
  {
    return 1280;
  }

  QUICPacketNumber
  largest_acked_packet_number(QUICEncryptionLevel level) const override
  {
    return 0;
  }

  NetVConnectionContext_t
  direction() const override
  {
    return _direction;
  }

  SSLNextProtocolSet *
  next_protocol_set() const override
  {
    return nullptr;
  }

  void
  close(QUICConnectionErrorUPtr error) override
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
    return &_stream_manager;
  }

  bool
  is_closed() const override
  {
    return false;
  }

  void
  handle_received_packet(UDPPacket *) override
  {
  }

  int _transmit_count   = 0;
  int _retransmit_count = 0;
  Ptr<ProxyMutex> _mutex;
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
  MockQUICStreamManager _stream_manager;

  QUICTransportParametersInEncryptedExtensions dummy_transport_parameters();
  NetVConnectionContext_t _direction;
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
  minimum_quic_packet_size() override
  {
    return 1200;
  }

  uint32_t
  maximum_quic_packet_size() const override
  {
    return 1200;
  }

  uint32_t
  pmtu() const override
  {
    return 1280;
  }

  QUICPacketNumber
  largest_acked_packet_number(QUICEncryptionLevel level) const override
  {
    return 0;
  }

  NetVConnectionContext_t
  direction() const override
  {
    return NET_VCONNECTION_OUT;
  }

  SSLNextProtocolSet *
  next_protocol_set() const override
  {
    return nullptr;
  }

  bool
  is_closed() const override
  {
    return false;
  }
};

class MockQUICPacketTransmitter : public QUICPacketTransmitter
{
public:
  MockQUICPacketTransmitter() : QUICPacketTransmitter() { this->_mutex = new_ProxyMutex(); };
  uint32_t
  transmit_packet(QUICPacketUPtr packet) override
  {
    if (packet) {
      this->transmitted.insert(packet->packet_number());
      return 1;
    }
    return 0;
  }

  void
  retransmit_packet(const QUICPacket &packet) override
  {
    this->retransmitted.insert(packet.packet_number());
  }

  Ptr<ProxyMutex>
  get_packet_transmitter_mutex() override
  {
    return this->_mutex;
  }

  Ptr<ProxyMutex> _mutex;

  std::set<QUICPacketNumber> transmitted;
  std::set<QUICPacketNumber> retransmitted;
};

class MockQUICCongestionController : public QUICCongestionController
{
public:
  MockQUICCongestionController(QUICConnectionInfoProvider *info) : QUICCongestionController(info) {}
  // Override
  virtual void
  on_packets_lost(const std::map<QUICPacketNumber, const PacketInfo *> &packets) override
  {
    for (auto &p : packets) {
      lost_packets.insert(p.first);
    }
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

class MockQUICLossDetector : public QUICLossDetector
{
public:
  MockQUICLossDetector(QUICPacketTransmitter *transmitter, QUICConnectionInfoProvider *info, QUICCongestionController *cc,
                       QUICRTTMeasure *rtt_measure, int index)
    : QUICLossDetector(transmitter, info, cc, rtt_measure, index)
  {
  }
  void
  rcv_frame(std::shared_ptr<const QUICFrame>)
  {
  }

  void
  on_packet_sent(QUICPacketUPtr packet)
  {
  }
};

class MockQUICApplication : public QUICApplication
{
public:
  MockQUICApplication() : QUICApplication(new MockQUICConnection) { SET_HANDLER(&MockQUICApplication::main_event_handler); }
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

class MockQUICStream : public QUICStream
{
public:
  MockQUICStream(QUICRTTProvider *rtt_provider, QUICConnectionInfoProvider *info, QUICStreamId sid, uint64_t recv_max_stream_data,
                 uint64_t send_max_stream_data)
    : QUICStream(rtt_provider, info, sid, recv_max_stream_data, send_max_stream_data)
  {
  }

private:
  int64_t
  _process_read_vio() override
  {
    return 0;
  }

  int64_t
  _process_write_vio() override
  {
    return 0;
  }
};

class MockQUICStreamIO : public QUICStreamIO
{
public:
  MockQUICStreamIO(QUICApplication *app, QUICStream *stream) : QUICStreamIO(app, stream) {}
  ~MockQUICStreamIO() {}
  int64_t
  transfer()
  {
    int64_t n = this->_write_buffer_reader->read_avail();
    this->_read_buffer->write(this->_write_buffer_reader, n);
    this->_write_buffer_reader->consume(n);
    return n;
  }

private:
  void
  read_reenable() override
  {
  }
  void
  write_reenable() override
  {
  }
};

class MockQUICHandshakeProtocol : public QUICHandshakeProtocol
{
public:
  MockQUICHandshakeProtocol() : QUICHandshakeProtocol() {}
  int
  handshake(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in) override
  {
    return true;
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

  bool
  is_key_derived(QUICKeyPhase /* key_phase */, bool /* for_encryption */) const override
  {
    return true;
  }

  int
  initialize_key_materials(QUICConnectionId cid) override
  {
    return 0;
  }

  int
  update_key_materials() override
  {
    return 0;
  }

  bool
  encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len, uint64_t pkt_num,
          const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const override
  {
    memcpy(cipher, plain, plain_len);
    return true;
  }

  bool
  decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len, uint64_t pkt_num,
          const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const override
  {
    memcpy(plain, cipher, cipher_len);
    return true;
  }

  bool
  encrypt_pn(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn, uint8_t unprotected_pn_len,
             const uint8_t *sample, QUICKeyPhase phase) const override
  {
    return true;
  }

  bool
  decrypt_pn(uint8_t *unprotected_pn, uint8_t &unprotected_pn_len, const uint8_t *protected_pn, uint8_t protected_pn_len,
             const uint8_t *sample, QUICKeyPhase phase) const override
  {
    return true;
  }

  QUICEncryptionLevel
  current_encryption_level() const override
  {
    return QUICEncryptionLevel::INITIAL;
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
  smoothed_rtt() const
  {
    return HRTIME_MSECONDS(1);
  }
};

class MockQUICTransferProgressProvider : public QUICTransferProgressProvider
{
  bool
  is_transfer_goal_set() const override
  {
    return false;
  }

  bool
  is_transfer_complete() const override
  {
    return false;
  }

  uint64_t
  transfer_progress() const override
  {
    return 0;
  }

  uint64_t
  transfer_goal() const override
  {
    return 0;
  }
};
