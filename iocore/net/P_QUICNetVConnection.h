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
#include "P_ALPNSupport.h"
#include "TLSSessionResumptionSupport.h"
#include "tscore/ink_apidefs.h"
#include "tscore/List.h"

#include "quic/QUICConfig.h"
#include "quic/QUICConnection.h"
#include "quic/QUICConnectionTable.h"
#include "quic/QUICResetTokenTable.h"
#include "quic/QUICVersionNegotiator.h"
#include "quic/QUICPacket.h"
#include "quic/QUICPacketFactory.h"
#include "quic/QUICFrame.h"
#include "quic/QUICFrameDispatcher.h"
#include "quic/QUICApplication.h"
#include "quic/QUICStream.h"
#include "quic/QUICHandshakeProtocol.h"
#include "quic/QUICAckFrameCreator.h"
#include "quic/QUICPinger.h"
#include "quic/QUICPadder.h"
#include "quic/QUICLossDetector.h"
#include "quic/QUICStreamManager.h"
#include "quic/QUICAltConnectionManager.h"
#include "quic/QUICPathValidator.h"
#include "quic/QUICPathManager.h"
#include "quic/QUICApplicationMap.h"
#include "quic/QUICPacketReceiveQueue.h"
#include "quic/QUICPacketHeaderProtector.h"
#include "quic/QUICAddrVerifyState.h"
#include "quic/QUICPacketProtectionKeyInfo.h"
#include "quic/QUICContext.h"
#include "quic/QUICTokenCreator.h"
#include "quic/qlog/QLogListener.h"

// Size of connection ids for debug log : e.g. aaaaaaaa-bbbbbbbb\0
static constexpr size_t MAX_CIDS_SIZE = 8 + 1 + 8 + 1;

//////////////////////////////////////////////////////////////////
//
//  class NetVConnection
//
//  A VConnection for a network socket.
//
//////////////////////////////////////////////////////////////////

class QUICPacketHandler;
class QUICLossDetector;
class QUICHandshake;

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
class QUICNetVConnection : public UnixNetVConnection,
                           public QUICConnection,
                           public RefCountObj,
                           public ALPNSupport,
                           public TLSSessionResumptionSupport
{
  using super = UnixNetVConnection; ///< Parent type.

public:
  QUICNetVConnection();
  ~QUICNetVConnection();
  void init(QUICVersion version, QUICConnectionId peer_cid, QUICConnectionId original_cid, UDPConnection *, QUICPacketHandler *,
            QUICResetTokenTable *rtable);
  void init(QUICVersion version, QUICConnectionId peer_cid, QUICConnectionId original_cid, QUICConnectionId first_cid,
            QUICConnectionId retry_cid, UDPConnection *, QUICPacketHandler *, QUICResetTokenTable *rtable,
            QUICConnectionTable *ctable);

  // accept new conn_id
  int acceptEvent(int event, Event *e);

  // NetVConnection
  void set_local_addr() override;

  // UnixNetVConnection
  void reenable(VIO *vio) override;
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override;
  int connectUp(EThread *t, int fd) override;

  // QUICNetVConnection
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

  int select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                           unsigned inlen) const override;

  // QUICConnection
  QUICStreamManager *stream_manager() override;
  void close_quic_connection(QUICConnectionErrorUPtr error) override;
  void reset_quic_connection() override;
  void handle_received_packet(UDPPacket *packet) override;
  void ping() override;

  // QUICConnection (QUICConnectionInfoProvider)
  QUICConnectionId peer_connection_id() const override;
  QUICConnectionId original_connection_id() const override;
  QUICConnectionId first_connection_id() const override;
  QUICConnectionId retry_source_connection_id() const override;
  QUICConnectionId initial_source_connection_id() const override;
  QUICConnectionId connection_id() const override;
  std::string_view cids() const override;
  const QUICFiveTuple five_tuple() const override;
  uint32_t pmtu() const override;
  NetVConnectionContext_t direction() const override;
  QUICVersion negotiated_version() const override;
  std::string_view negotiated_application_name() const override;
  bool is_closed() const override;
  bool is_at_anti_amplification_limit() const override;
  bool is_address_validation_completed() const override;
  bool is_handshake_completed() const override;
  bool has_keys_for(QUICPacketNumberSpace space) const override;

  // QUICConnection (QUICFrameHandler)
  std::vector<QUICFrameType> interests() override;
  QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;

  int in_closed_queue = 0;

  bool shouldDestroy();

  LINK(QUICNetVConnection, closed_link);
  SLINK(QUICNetVConnection, closed_alink);

protected:
  const IpEndpoint &_getLocalEndpoint() override;

private:
  std::random_device _rnd;

  QUICConfig::scoped_config _quic_config;

  QUICConnectionId _peer_quic_connection_id;      // dst cid in local
  QUICConnectionId _peer_old_quic_connection_id;  // dst previous cid in local
  QUICConnectionId _original_quic_connection_id;  // dst cid of initial packet from client
  QUICConnectionId _first_quic_connection_id;     // dst cid of initial packet from client that doesn't have retry token
  QUICConnectionId _retry_source_connection_id;   // src cid used for sending Retry packet
  QUICConnectionId _initial_source_connection_id; // src cid used for Initial packet
  QUICConnectionId _quic_connection_id;           // src cid in local
  QUICFiveTuple _five_tuple;
  bool _connection_migration_initiated = false;

  char _cids_data[MAX_CIDS_SIZE] = {0};
  std::string_view _cids;

  QUICVersion _initial_version;
  UDPConnection *_udp_con = nullptr;
  QUICPacketProtectionKeyInfo _pp_key_info;
  QUICPacketHandler *_packet_handler = nullptr;
  QUICPacketFactory _packet_factory;
  QUICFrameFactory _frame_factory;
  QUICAckFrameManager _ack_frame_manager;
  QUICPacketHeaderProtector _ph_protector;
  QUICRTTMeasure _rtt_measure;
  QUICApplicationMap *_application_map = nullptr;

  uint32_t _pmtu = 1280;

  // TODO: use custom allocator and make them std::unique_ptr or std::shared_ptr
  // or make them just member variables.
  QUICPinger *_pinger                               = nullptr;
  QUICPadder *_padder                               = nullptr;
  QUICHandshake *_handshake_handler                 = nullptr;
  QUICHandshakeProtocol *_hs_protocol               = nullptr;
  QUICLossDetector *_loss_detector                  = nullptr;
  QUICFrameDispatcher *_frame_dispatcher            = nullptr;
  QUICStreamManager *_stream_manager                = nullptr;
  QUICCongestionController *_congestion_controller  = nullptr;
  QUICRemoteFlowController *_remote_flow_controller = nullptr;
  QUICLocalFlowController *_local_flow_controller   = nullptr;
  QUICResetTokenTable *_rtable                      = nullptr;
  QUICConnectionTable *_ctable                      = nullptr;
  QUICAltConnectionManager *_alt_con_manager        = nullptr;
  QUICPathValidator *_path_validator                = nullptr;
  QUICPathManager *_path_manager                    = nullptr;
  QUICTokenCreator *_token_creator                  = nullptr;

  QUICFrameGeneratorManager _frame_generators;

  QUICPacketReceiveQueue _packet_recv_queue = {this->_packet_factory, this->_ph_protector};

  QUICConnectionErrorUPtr _connection_error  = nullptr;
  uint32_t _state_closing_recv_packet_count  = 0;
  uint32_t _state_closing_recv_packet_window = 1;
  uint64_t _flow_control_buffer_size         = 1024;

  void _init_submodules();

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

  void _schedule_ack_manager_periodic(ink_hrtime interval);
  void _unschedule_ack_manager_periodic();
  Event *_ack_manager_periodic = nullptr;

  QUICEncryptionLevel _minimum_encryption_level = QUICEncryptionLevel::INITIAL;

  QUICPacketNumber _largest_acked_packet_number(QUICEncryptionLevel level) const;
  uint32_t _maximum_quic_packet_size() const;
  uint32_t _minimum_quic_packet_size();
  uint64_t _maximum_stream_frame_data_size();

  Ptr<IOBufferBlock> _store_frame(Ptr<IOBufferBlock> parent_block, size_t &size_added, uint64_t &max_frame_size, QUICFrame &frame,
                                  std::vector<QUICSentPacketInfo::FrameInfo> &frames);
  QUICPacketUPtr _packetize_frames(uint8_t *packet_buf, QUICEncryptionLevel level, uint64_t max_packet_size,
                                   std::vector<QUICSentPacketInfo::FrameInfo> &frames);
  void _packetize_closing_frame();
  QUICPacketUPtr _build_packet(uint8_t *packet_buf, QUICEncryptionLevel level, const Ptr<IOBufferBlock> &parent_block,
                               bool retransmittable, bool probing, bool crypto);

  QUICConnectionErrorUPtr _recv_and_ack(const QUICPacketR &packet, bool *has_non_probing_frame = nullptr);

  QUICConnectionErrorUPtr _state_handshake_process_packet(const QUICPacket &packet);
  QUICConnectionErrorUPtr _state_handshake_process_version_negotiation_packet(const QUICVersionNegotiationPacketR &packet);
  QUICConnectionErrorUPtr _state_handshake_process_initial_packet(const QUICInitialPacketR &packet);
  QUICConnectionErrorUPtr _state_handshake_process_retry_packet(const QUICRetryPacketR &packet);
  QUICConnectionErrorUPtr _state_handshake_process_handshake_packet(const QUICHandshakePacketR &packet);
  QUICConnectionErrorUPtr _state_handshake_process_zero_rtt_protected_packet(const QUICZeroRttPacketR &packet);
  QUICConnectionErrorUPtr _state_connection_established_receive_packet();
  QUICConnectionErrorUPtr _state_connection_established_process_protected_packet(const QUICShortHeaderPacketR &packet);
  QUICConnectionErrorUPtr _state_connection_established_migrate_connection(const QUICPacketR &packet);
  QUICConnectionErrorUPtr _state_connection_established_initiate_connection_migration();
  QUICConnectionErrorUPtr _state_closing_receive_packet();
  QUICConnectionErrorUPtr _state_draining_receive_packet();
  QUICConnectionErrorUPtr _state_common_send_packet();
  QUICConnectionErrorUPtr _state_handshake_send_retry_packet();
  QUICConnectionErrorUPtr _state_closing_send_packet();

  Ptr<ProxyMutex> _packet_transmitter_mutex;

  void _init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                 const std::shared_ptr<const QUICTransportParameters> &remote_tp);
  void _handle_error(QUICConnectionErrorUPtr error);
  QUICPacketUPtr _dequeue_recv_packet(uint8_t *packet_buf, QUICPacketCreationResult &result);
  void _validate_new_path(const QUICPath &path);

  bool _handshake_completed = false;
  int _complete_handshake_if_possible();

  void _switch_to_handshake_state();
  void _switch_to_established_state();
  void _switch_to_closing_state(QUICConnectionErrorUPtr error);
  void _switch_to_draining_state(QUICConnectionErrorUPtr error);
  void _switch_to_close_state();

  bool _application_started = false;
  void _start_application();

  void _handle_periodic_ack_event();
  void _handle_idle_timeout();
  void _handle_active_timeout();

  QUICConnectionErrorUPtr _handle_frame(const QUICNewConnectionIdFrame &frame);

  void _update_cids();
  void _update_peer_cid(const QUICConnectionId &new_cid);
  void _update_local_cid(const QUICConnectionId &new_cid);
  void _rerandomize_original_cid();

  QUICHandshakeProtocol *_setup_handshake_protocol(const shared_SSL_CTX &ctx);

  QUICPacketUPtr _the_final_packet = QUICPacketFactory::create_null_packet();
  uint8_t _final_packet_buf[QUICPacket::MAX_INSTANCE_SIZE];
  QUICStatelessResetToken _reset_token;

  ats_unique_buf _av_token = {nullptr};
  size_t _av_token_len     = 0;

  uint64_t _stream_frames_sent = 0;
  uint32_t _seq_num            = 0;

  // TODO: Source addresses verification through an address validation token
  QUICAddrVerifyState _verified_state;

  std::unique_ptr<QUICContext> _context;

  std::shared_ptr<QLog::QLogListener> _qlog;
};

typedef int (QUICNetVConnection::*QUICNetVConnHandler)(int, void *);

extern ClassAllocator<QUICNetVConnection> quicNetVCAllocator;
