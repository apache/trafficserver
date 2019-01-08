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

#include "tscore/ink_platform.h"
#include "P_Net.h"
#include "P_EventSystem.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixNet.h"
#include "P_UDPNet.h"
#include "tscore/ink_apidefs.h"
#include "tscore/List.h"

#include "quic/QUICConnection.h"
#include "quic/QUICConnectionTable.h"
#include "quic/QUICVersionNegotiator.h"
#include "quic/QUICPacket.h"
#include "quic/QUICFrame.h"
#include "quic/QUICFrameDispatcher.h"
#include "quic/QUICHandshake.h"
#include "quic/QUICApplication.h"
#include "quic/QUICStream.h"
#include "quic/QUICHandshakeProtocol.h"
#include "quic/QUICAckFrameCreator.h"
#include "quic/QUICPinger.h"
#include "quic/QUICPacketRetransmitter.h"
#include "quic/QUICLossDetector.h"
#include "quic/QUICStreamManager.h"
#include "quic/QUICAltConnectionManager.h"
#include "quic/QUICPathValidator.h"
#include "quic/QUICApplicationMap.h"
#include "quic/QUICPacketReceiveQueue.h"

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

// Size of connection ids for debug log : e.g. aaaaaaaa-bbbbbbbb\0
static constexpr size_t MAX_CIDS_SIZE = 8 + 1 + 8 + 1;

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
 *  |   _state_handshake_process_initial_packet()
 *  |   _state_handshake_process_retry_packet()
 *  |   _state_handshake_process_handshake_packet()
 *  |   _state_handshake_process_zero_rtt_protected_packet()
 *  | WRITE:
 *  |   _state_common_send_packet()
 *  |   or
 *  |   _state_handshake_send_retry_packet()
 *  v
 * state_connection_established()
 *  | READ:
 *  |   _state_connection_established_receive_packet()
 *  |   _state_connection_established_process_protected_packet()
 *  | WRITE:
 *  |   _state_common_send_packet()
 *  v
 * state_connection_closing() (If closing actively)
 *  | READ:
 *  |   _state_closing_receive_packet()
 *  | WRITE:
 *  |   _state_closing_send_packet()
 *  v
 * state_connection_draining() (If closing passively)
 *  | READ:
 *  |   _state_draining_receive_packet()
 *  | WRITE:
 *  |   Do nothing
 *  v
 * state_connection_close()
 *    READ:
 *      Do nothing
 *    WRITE:
 *      Do nothing
 **/
class QUICNetVConnection : public UnixNetVConnection, public QUICConnection, public QUICFrameGenerator, public RefCountObj
{
  using super = UnixNetVConnection; ///< Parent type.

public:
  QUICNetVConnection();
  ~QUICNetVConnection();
  void init(QUICConnectionId peer_cid, QUICConnectionId original_cid, UDPConnection *, QUICPacketHandler *);
  void init(QUICConnectionId peer_cid, QUICConnectionId original_cid, QUICConnectionId first_cid, UDPConnection *,
            QUICPacketHandler *, QUICConnectionTable *ctable);

  // accept new conn_id
  int acceptEvent(int event, Event *e);

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
  void start();
  void remove_connection_ids();
  void free(EThread *t) override;
  void free() override;
  void destroy(EThread *t);

  UDPConnection *get_udp_con();
  virtual void net_read_io(NetHandler *nh, EThread *lthread) override;
  virtual int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs) override;

  int populate_protocol(std::string_view *results, int n) const override;
  const char *protocol_contains(std::string_view tag) const override;

  // QUICNetVConnection
  void registerNextProtocolSet(SSLNextProtocolSet *s);

  // QUICConnection
  QUICStreamManager *stream_manager() override;
  void close(QUICConnectionErrorUPtr error) override;
  void handle_received_packet(UDPPacket *packet) override;
  void ping() override;

  // QUICConnection (QUICConnectionInfoProvider)
  QUICConnectionId peer_connection_id() const override;
  QUICConnectionId original_connection_id() const override;
  QUICConnectionId first_connection_id() const override;
  QUICConnectionId connection_id() const override;
  std::string_view cids() const override;
  const QUICFiveTuple five_tuple() const override;
  uint32_t maximum_quic_packet_size() const override;
  uint32_t minimum_quic_packet_size() override;
  uint32_t pmtu() const override;
  NetVConnectionContext_t direction() const override;
  SSLNextProtocolSet *next_protocol_set() const override;
  QUICPacketNumber largest_acked_packet_number(QUICEncryptionLevel level) const override;
  bool is_closed() const override;

  // QUICConnection (QUICPacketTransmitter)
  virtual void retransmit_packet(const QUICPacket &packet) override;
  virtual Ptr<ProxyMutex> get_packet_transmitter_mutex() override;

  // QUICConnection (QUICFrameHandler)
  std::vector<QUICFrameType> interests() override;
  QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level) override;
  QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size) override;

  int in_closed_queue = 0;

  bool shouldDestroy();

  LINK(QUICNetVConnection, closed_link);
  SLINK(QUICNetVConnection, closed_alink);

private:
  QUICPacketType _last_received_packet_type = QUICPacketType::UNINITIALIZED;
  std::random_device _rnd;

  QUICConnectionId _peer_quic_connection_id;     // dst cid in local
  QUICConnectionId _peer_old_quic_connection_id; // dst previous cid in local
  QUICConnectionId _original_quic_connection_id; // dst cid of initial packet from client
  QUICConnectionId _first_quic_connection_id;    // dst cid of initial packet from client that doesn't have retry token
  QUICConnectionId _quic_connection_id;          // src cid in local
  QUICFiveTuple _five_tuple;
  bool _connection_migration_initiated = false;

  char _cids_data[MAX_CIDS_SIZE] = {0};
  std::string_view _cids;

  UDPConnection *_udp_con            = nullptr;
  QUICPacketHandler *_packet_handler = nullptr;
  QUICPacketFactory _packet_factory;
  QUICFrameFactory _frame_factory;
  QUICAckFrameManager _ack_frame_manager;
  QUICPinger _pinger;
  QUICPacketRetransmitter _packet_retransmitter;
  QUICPacketNumberProtector _pn_protector;
  QUICRTTMeasure _rtt_measure;
  QUICApplicationMap *_application_map = nullptr;

  uint32_t _pmtu = 1280;

  SSLNextProtocolSet *_next_protocol_set = nullptr;

  // TODO: use custom allocator and make them std::unique_ptr or std::shared_ptr
  // or make them just member variables.
  QUICHandshake *_handshake_handler                 = nullptr;
  QUICHandshakeProtocol *_hs_protocol               = nullptr;
  QUICLossDetector *_loss_detector[3]               = {nullptr};
  QUICFrameDispatcher *_frame_dispatcher            = nullptr;
  QUICStreamManager *_stream_manager                = nullptr;
  QUICCongestionController *_congestion_controller  = nullptr;
  QUICRemoteFlowController *_remote_flow_controller = nullptr;
  QUICLocalFlowController *_local_flow_controller   = nullptr;
  QUICConnectionTable *_ctable                      = nullptr;
  QUICAltConnectionManager *_alt_con_manager        = nullptr;
  QUICPathValidator *_path_validator                = nullptr;

  QUICPacketReceiveQueue _packet_recv_queue = {this->_packet_factory, this->_pn_protector};

  QUICConnectionErrorUPtr _connection_error  = nullptr;
  uint32_t _state_closing_recv_packet_count  = 0;
  uint32_t _state_closing_recv_packet_window = 1;
  uint64_t _flow_control_buffer_size         = 1024;

  void _schedule_packet_write_ready(bool delay = false);
  void _unschedule_packet_write_ready();
  void _close_packet_write_ready(Event *data);
  Event *_packet_write_ready = nullptr;

  void _schedule_closing_timeout(ink_hrtime interval);
  void _unschedule_closing_timeout();
  void _close_closing_timeout(Event *data);
  Event *_closing_timeout = nullptr;

  void _schedule_closed_event();
  void _unschedule_closed_event();
  void _close_closed_event(Event *data);
  Event *_closed_event = nullptr;

  void _schedule_path_validation_timeout(ink_hrtime interval);
  void _unschedule_path_validation_timeout();
  void _close_path_validation_timeout(Event *data);
  Event *_path_validation_timeout = nullptr;

  void _unschedule_ack_manager_periodic();
  Event *_ack_manager_periodic = nullptr;
  bool _refresh_ack_frame_manager();

  uint64_t _maximum_stream_frame_data_size();
  void _store_frame(ats_unique_buf &buf, size_t &offset, uint64_t &max_frame_size, QUICFrameUPtr &frame,
                    std::vector<QUICFrameInfo> &frames);
  QUICPacketUPtr _packetize_frames(QUICEncryptionLevel level, uint64_t max_packet_size);
  void _packetize_closing_frame();
  QUICPacketUPtr _build_packet(ats_unique_buf buf, size_t len, bool retransmittable, bool probing,
                               std::vector<QUICFrameInfo> &frames, QUICPacketType type = QUICPacketType::UNINITIALIZED);
  QUICPacketUPtr _build_packet(QUICEncryptionLevel level, ats_unique_buf buf, size_t len, bool retransmittable, bool probing,
                               std::vector<QUICFrameInfo> &frames);

  QUICConnectionErrorUPtr _recv_and_ack(QUICPacket &packet, bool *has_non_probing_frame = nullptr);

  QUICConnectionErrorUPtr _state_handshake_process_packet(QUICPacketUPtr packet);
  QUICConnectionErrorUPtr _state_handshake_process_version_negotiation_packet(QUICPacketUPtr packet);
  QUICConnectionErrorUPtr _state_handshake_process_initial_packet(QUICPacketUPtr packet);
  QUICConnectionErrorUPtr _state_handshake_process_retry_packet(QUICPacketUPtr packet);
  QUICConnectionErrorUPtr _state_handshake_process_handshake_packet(QUICPacketUPtr packet);
  QUICConnectionErrorUPtr _state_handshake_process_zero_rtt_protected_packet(QUICPacketUPtr packet);
  QUICConnectionErrorUPtr _state_connection_established_receive_packet();
  QUICConnectionErrorUPtr _state_connection_established_process_protected_packet(QUICPacketUPtr packet);
  QUICConnectionErrorUPtr _state_connection_established_migrate_connection(const QUICPacket &p);
  QUICConnectionErrorUPtr _state_connection_established_initiate_connection_migration();
  QUICConnectionErrorUPtr _state_closing_receive_packet();
  QUICConnectionErrorUPtr _state_draining_receive_packet();
  QUICConnectionErrorUPtr _state_common_send_packet();
  QUICConnectionErrorUPtr _state_handshake_send_retry_packet();
  QUICConnectionErrorUPtr _state_closing_send_packet();

  Ptr<ProxyMutex> _packet_transmitter_mutex;
  Ptr<ProxyMutex> _frame_transmitter_mutex;

  void _init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                 const std::shared_ptr<const QUICTransportParameters> &remote_tp);
  void _handle_error(QUICConnectionErrorUPtr error);
  QUICPacketUPtr _dequeue_recv_packet(QUICPacketCreationResult &result);
  void _validate_new_path();

  int _complete_handshake_if_possible();
  void _switch_to_handshake_state();
  void _switch_to_established_state();
  void _switch_to_closing_state(QUICConnectionErrorUPtr error);
  void _switch_to_draining_state(QUICConnectionErrorUPtr error);
  void _switch_to_close_state();

  bool _application_started = false;
  void _start_application();

  void _handle_path_validation_timeout(Event *data);
  void _handle_idle_timeout();

  QUICConnectionErrorUPtr _handle_frame(const QUICNewConnectionIdFrame &frame);

  void _update_cids();
  void _update_peer_cid(const QUICConnectionId &new_cid);
  void _update_local_cid(const QUICConnectionId &new_cid);
  void _rerandomize_original_cid();
  bool _is_src_addr_verified();

  QUICHandshakeProtocol *_setup_handshake_protocol(SSL_CTX *ctx);

  QUICPacketUPtr _the_final_packet = QUICPacketFactory::create_null_packet();
  QUICStatelessResetToken _reset_token;

  ats_unique_buf _av_token       = {nullptr};
  size_t _av_token_len           = 0;
  bool _is_resumption_token_sent = false;

  // This is for limiting number of packets that a server can send without path validation
  uint32_t _handshake_packets_sent = 0;
  uint64_t _stream_frames_sent     = 0;

  // TODO: Source addresses verification through an address validation token
  bool _src_addr_verified       = false;
  bool _has_ack_only_packet_out = false;

  // QUICFrameGenerator
  void _on_frame_lost(QUICFrameInformation &info) override;
  std::vector<QUICEncryptionLevel>
  _encryption_level_filter() override
  {
    return {
      QUICEncryptionLevel::ONE_RTT,
    };
  }
};

typedef int (QUICNetVConnection::*QUICNetVConnHandler)(int, void *);

extern ClassAllocator<QUICNetVConnection> quicNetVCAllocator;
