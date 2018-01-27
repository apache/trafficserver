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

  QUICNetVConnection.h

  This file implements an I/O Processor for network I/O.


 ****************************************************************************/
#pragma once

#include <queue>

#include "ts/ink_platform.h"
#include "P_Net.h"
#include "P_EventSystem.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixNet.h"
#include "P_UDPNet.h"
#include "ts/apidefs.h"
#include "ts/List.h"

#include "quic/QUICConnection.h"
#include "quic/QUICVersionNegotiator.h"
#include "quic/QUICPacket.h"
#include "quic/QUICFrame.h"
#include "quic/QUICFrameDispatcher.h"
#include "quic/QUICHandshake.h"
#include "quic/QUICApplication.h"
#include "quic/QUICStream.h"
#include "quic/QUICCrypto.h"
#include "quic/QUICAckFrameCreator.h"
#include "quic/QUICLossDetector.h"
#include "quic/QUICStreamManager.h"
#include "quic/QUICApplicationMap.h"

// These are included here because older OpenQUIC libraries don't have them.
// Don't copy these defines, or use their values directly, they are merely
// here to avoid compiler errors.
#ifndef QUIC_TLSEXT_ERR_OK
#define QUIC_TLSEXT_ERR_OK 0
#endif

#ifndef QUIC_TLSEXT_ERR_NOACK
#define QUIC_TLSEXT_ERR_NOACK 3
#endif

#define QUIC_OP_HANDSHAKE 0x16

// class QUICNextProtocolSet;
// struct QUICCertLookup;

typedef enum {
  QUIC_HOOK_OP_DEFAULT,                      ///< Null / initialization value. Do normal processing.
  QUIC_HOOK_OP_TUNNEL,                       ///< Switch to blind tunnel
  QUIC_HOOK_OP_TERMINATE,                    ///< Termination connection / transaction.
  QUIC_HOOK_OP_LAST = QUIC_HOOK_OP_TERMINATE ///< End marker value.
} QuicVConnOp;

//////////////////////////////////////////////////////////////////
//
//  class NetVConnection
//
//  A VConnection for a network socket.
//
//////////////////////////////////////////////////////////////////

class QUICPacketHandler;
class QUICLossDetector;

class SSLNextProtocolSet;

/**
 * @class QUICNetVConnection
 * @brief A NetVConnection for a QUIC network socket
 * @detail
 *
 * state_pre_handshake()
 *  | READ:
 *  |   Do nothing
 *  | WRITE:
 *  |   _state_common_send_packet()
 *  v
 * state_handshake()
 *  | READ:
 *  |   _state_handshake_process_packet()
 *  |   _state_handshake_process_initial_client_packet()
 *  |   _state_handshake_process_client_cleartext_packet()
 *  |   _state_handshake_process_zero_rtt_protected_packet()
 *  | WRITE:
 *  |   _state_common_send_packet()
 *  v
 * state_connection_established()
 *  | READ:
 *  |   _state_connection_established_process_packet()
 *  | WRITE:
 *  |   _state_common_send_packet()
 *  v
 * state_connection_closing() (If closing actively)
 *  | READ:
 *  |   _state_connection_established_process_packet()
 *  | WRITE:
 *  |   _state_common_send_packet()
 *  v
 * state_connection_draining() (If closing passively)
 *  | READ:
 *  |   _state_connection_established_process_packet()
 *  | WRITE:
 *  |   Do nothing
 *  v
 * state_connection_close()
 *    READ:
 *      Do nothing
 *    WRITE:
 *      Do nothing
 **/
class QUICNetVConnection : public UnixNetVConnection, public QUICConnection
{
  using super = UnixNetVConnection; ///< Parent type.

public:
  QUICNetVConnection() {}
  void init(QUICConnectionId cid, UDPConnection *, QUICPacketHandler *);

  // UnixNetVConnection
  void reenable(VIO *vio) override;
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override;
  int connectUp(EThread *t, int fd) override;

  // QUICNetVConnection
  int startEvent(int event, Event *e);
  int state_pre_handshake(int event, Event *data);
  int state_handshake(int event, Event *data);
  int state_connection_established(int event, Event *data);
  int state_connection_closing(int event, Event *data);
  int state_connection_draining(int event, Event *data);
  int state_connection_closed(int event, Event *data);
  void start(SSL_CTX *);
  void push_packet(UDPPacket *packet);
  void free(EThread *t) override;

  UDPConnection *get_udp_con();
  virtual void net_read_io(NetHandler *nh, EThread *lthread) override;
  virtual int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs) override;

  int populate_protocol(ts::string_view *results, int n) const override;
  const char *protocol_contains(ts::string_view tag) const override;

  // QUICNetVConnection
  void registerNextProtocolSet(SSLNextProtocolSet *s);
  bool is_closed();

  // QUICConnection
  QUICConnectionId original_connection_id() override;
  QUICConnectionId connection_id() override;
  uint32_t maximum_quic_packet_size() override;
  uint32_t minimum_quic_packet_size() override;
  uint32_t maximum_stream_frame_data_size() override;
  QUICStreamManager *stream_manager() override;
  uint32_t pmtu() override;
  NetVConnectionContext_t direction() override;
  SSLNextProtocolSet *next_protocol_set() override;
  void close(QUICConnectionErrorUPtr error) override;
  QUICPacketNumber largest_received_packet_number() override;
  QUICPacketNumber largest_acked_packet_number() override;

  // QUICConnection (QUICPacketTransmitter)
  virtual uint32_t transmit_packet(QUICPacketUPtr packet) override;
  virtual void retransmit_packet(const QUICPacket &packet) override;
  virtual Ptr<ProxyMutex> get_packet_transmitter_mutex() override;

  // QUICConnection (QUICFrameTransmitter)
  virtual void transmit_frame(QUICFrameUPtr frame) override;

  // QUICConnection (QUICFrameHandler)
  std::vector<QUICFrameType> interests() override;
  QUICErrorUPtr handle_frame(std::shared_ptr<const QUICFrame> frame) override;

private:
  class AltConnectionInfo
  {
  public:
    int seq_num;
    QUICConnectionId id;
    QUICStatelessResetToken token;
  };

  std::random_device _rnd;

  QUICConnectionId _original_quic_connection_id;
  QUICConnectionId _quic_connection_id;

  AltConnectionInfo _alt_quic_connection_ids[3];
  int8_t _alt_quic_connection_id_seq_num = 0;

  QUICPacketNumber _largest_received_packet_number = 0;
  UDPConnection *_udp_con                          = nullptr;
  QUICPacketHandler *_packet_handler               = nullptr;
  QUICPacketFactory _packet_factory;
  QUICFrameFactory _frame_factory;
  QUICAckFrameCreator _ack_frame_creator;
  QUICApplicationMap *_application_map = nullptr;

  uint32_t _pmtu = 1280;

  SSLNextProtocolSet *_next_protocol_set = nullptr;

  // TODO: use custom allocator and make them std::unique_ptr or std::shared_ptr
  // or make them just member variables.
  QUICHandshake *_handshake_handler                 = nullptr;
  QUICCrypto *_crypto                               = nullptr;
  QUICLossDetector *_loss_detector                  = nullptr;
  QUICFrameDispatcher *_frame_dispatcher            = nullptr;
  QUICStreamManager *_stream_manager                = nullptr;
  QUICCongestionController *_congestion_controller  = nullptr;
  QUICRemoteFlowController *_remote_flow_controller = nullptr;
  QUICLocalFlowController *_local_flow_controller   = nullptr;

  CountQueue<UDPPacket> _packet_recv_queue;
  CountQueue<QUICPacket> _packet_send_queue;
  std::queue<QUICPacketUPtr> _quic_packet_recv_queue;
  // `_frame_send_queue` is the queue for any type of frame except STREAM frame.
  // The flow contorl doesn't blcok frames in this queue.
  // `_stream_frame_send_queue` is the queue for STREAM frame.
  std::queue<QUICFrameUPtr> _frame_send_queue;
  std::queue<QUICFrameUPtr> _stream_frame_send_queue;

  void _schedule_packet_write_ready();
  void _unschedule_packet_write_ready();
  void _close_packet_write_ready(Event *data);
  Event *_packet_write_ready = nullptr;

  void _schedule_closing_timeout(ink_hrtime interval);
  void _unschedule_closing_timeout();
  void _close_closing_timeout(Event *data);
  Event *_closing_timeout = nullptr;

  uint32_t _transmit_packet(QUICPacketUPtr);
  void _transmit_frame(QUICFrameUPtr);

  void _store_frame(ats_unique_buf &buf, size_t &len, bool &retransmittable, QUICPacketType &current_packet_type,
                    QUICFrameUPtr frame);
  void _packetize_frames();
  QUICPacketUPtr _build_packet(ats_unique_buf buf, size_t len, bool retransmittable,
                               QUICPacketType type = QUICPacketType::UNINITIALIZED);

  QUICErrorUPtr _recv_and_ack(const uint8_t *payload, uint16_t size, QUICPacketNumber packet_numm);

  QUICErrorUPtr _state_handshake_process_packet(QUICPacketUPtr packet);
  QUICErrorUPtr _state_handshake_process_initial_client_packet(QUICPacketUPtr packet);
  QUICErrorUPtr _state_handshake_process_client_cleartext_packet(QUICPacketUPtr packet);
  QUICErrorUPtr _state_handshake_process_zero_rtt_protected_packet(QUICPacketUPtr packet);
  QUICErrorUPtr _state_connection_established_process_packet(QUICPacketUPtr packet);
  QUICErrorUPtr _state_common_receive_packet();
  QUICErrorUPtr _state_common_send_packet();
  QUICErrorUPtr _state_closing_send_packet();

  Ptr<ProxyMutex> _packet_transmitter_mutex;
  Ptr<ProxyMutex> _frame_transmitter_mutex;

  void _init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                 const std::shared_ptr<const QUICTransportParameters> &remote_tp);
  void _handle_error(QUICErrorUPtr error);
  QUICPacketUPtr _dequeue_recv_packet(QUICPacketCreationResult &result);

  int _complete_handshake_if_possible();
  void _switch_to_handshake_state();
  void _switch_to_established_state();
  void _switch_to_closing_state(QUICConnectionErrorUPtr error);
  void _switch_to_draining_state(QUICConnectionErrorUPtr error);
  void _switch_to_close_state();

  void _handle_idle_timeout();
  void _update_alt_connection_ids(uint8_t chosen);

  QUICPacketUPtr _the_final_packet = QUICPacketFactory::create_null_packet();
  QUICStatelessResetToken _reset_token;
};

extern ClassAllocator<QUICNetVConnection> quicNetVCAllocator;
