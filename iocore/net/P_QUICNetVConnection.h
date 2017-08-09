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
#include "quic/QUICPacketTransmitter.h"
#include "quic/QUICFrameTransmitter.h"
#include "quic/QUICStreamManager.h"
#include "quic/QUICConnectionManager.h"
#include "quic/QUICFlowController.h"
#include "quic/QUICCongestionController.h"

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

// TS-2503: dynamic TLS record sizing
// For smaller records, we should also reserve space for various TCP options
// (timestamps, SACKs.. up to 40 bytes [1]), and account for TLS record overhead
// (another 20-60 bytes on average, depending on the negotiated ciphersuite [2]).
// All in all: 1500 - 40 (IP) - 20 (TCP) - 40 (TCP options) - TLS overhead (60-100)
// For larger records, the size is determined by TLS protocol record size
#define QUIC_DEF_TLS_RECORD_SIZE 1300  // 1500 - 40 (IP) - 20 (TCP) - 40 (TCP options) - TLS overhead (60-100)
#define QUIC_MAX_TLS_RECORD_SIZE 16383 // 2^14 - 1
#define QUIC_DEF_TLS_RECORD_BYTE_THRESHOLD 1000000
#define QUIC_DEF_TLS_RECORD_MSEC_THRESHOLD 1000

// class QUICNextProtocolSet;
// struct QUICCertLookup;

typedef enum {
  QUIC_HOOK_OP_DEFAULT,                      ///< Null / initialization value. Do normal processing.
  QUIC_HOOK_OP_TUNNEL,                       ///< Switch to blind tunnel
  QUIC_HOOK_OP_TERMINATE,                    ///< Termination connection / transaction.
  QUIC_HOOK_OP_LAST = QUIC_HOOK_OP_TERMINATE ///< End marker value.
} QuicVConnOp;

enum class QUICConnectionState {
  Open = 0,
  Handshake,
  Established,
  TimeWait,
  Closing,
  Closed,
};

//////////////////////////////////////////////////////////////////
//
//  class NetVConnection
//
//  A VConnection for a network socket.
//
//////////////////////////////////////////////////////////////////

typedef std::unique_ptr<uint8_t> ats_uint8_t_unique_ptr;

struct QUICPacketHandler;
class QUICLossDetector;

/**
 * @class QUICNetVConnection
 * @brief A NetVConnection for a QUIC network socket
 * @detail
 *
 * state_handshake()
 *  | READ:
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
 * state_connection_close()
 *    READ:
 *      Do nothing
 *    WRITE:
 *      _state_common_send_packet()
 **/
class QUICNetVConnection : public UnixNetVConnection, public QUICPacketTransmitter, public QUICFrameTransmitter
{
  typedef UnixNetVConnection super; ///< Parent type.

public:
  QUICNetVConnection();

  void init(UDPConnection *, QUICPacketHandler *);

  void reenable(VIO *vio) override;
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override;
  int startEvent(int event, Event *e);
  int state_handshake(int event, Event *data);
  int state_connection_established(int event, Event *data);
  int state_connection_closed(int event, Event *data);
  void start(SSL_CTX *);
  uint32_t maximum_quic_packet_size();
  uint32_t minimum_quic_packet_size();
  virtual void transmit_packet(std::unique_ptr<const QUICPacket> packet) override;
  virtual void retransmit_packet(const QUICPacket &packet) override;
  virtual Ptr<ProxyMutex> get_transmitter_mutex() override;
  void push_packet(std::unique_ptr<const QUICPacket> packet);
  virtual void transmit_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame) override;
  void close(QUICError error);
  void free(EThread *t) override;

  UDPConnection *get_udp_con();
  QUICApplication *get_application(QUICStreamId stream_id);
  QUICCrypto *get_crypto();

  virtual void net_read_io(NetHandler *nh, EThread *lthread) override;
  virtual int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs) override;

private:
  QUICConnectionId _quic_connection_id;
  UDPConnection *_udp_con            = nullptr;
  QUICPacketHandler *_packet_handler = nullptr;
  QUICPacketFactory _packet_factory;
  QUICFrameFactory _frame_factory;
  QUICAckFrameCreator _ack_frame_creator;
  QUICConnectionState _state = QUICConnectionState::Open;

  uint32_t _pmtu = 1280;

  // TODO: use custom allocator and make them std::unique_ptr or std::shared_ptr
  // or make them just member variables.
  QUICVersionNegotiator *_version_negotiator         = nullptr;
  QUICHandshake *_handshake_handler                  = nullptr;
  QUICApplication *_application                      = nullptr;
  QUICCrypto *_crypto                                = nullptr;
  std::shared_ptr<QUICLossDetector> _loss_detector   = nullptr;
  std::shared_ptr<QUICStreamManager> _stream_manager = nullptr;
  QUICFrameDispatcher *_frame_dispatcher             = nullptr;

  Queue<QUICPacket> _packet_recv_queue;
  Queue<QUICPacket> _packet_send_queue;
  std::queue<std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>> _frame_buffer;

  void _packetize_frames();
  std::unique_ptr<QUICPacket> _build_packet(ats_unique_buf buf, size_t len, bool retransmittable,
                                            QUICPacketType type = QUICPacketType::UNINITIALIZED);

  void _recv_and_ack(const uint8_t *payload, uint16_t size, QUICPacketNumber packet_num);

  QUICError _state_handshake_process_initial_client_packet(std::unique_ptr<const QUICPacket> packet);
  QUICError _state_handshake_process_client_cleartext_packet(std::unique_ptr<const QUICPacket> packet);
  QUICError _state_handshake_process_zero_rtt_protected_packet(std::unique_ptr<const QUICPacket> packet);
  QUICError _state_connection_established_process_packet(std::unique_ptr<const QUICPacket> packet);
  QUICError _state_common_send_packet();

  Ptr<ProxyMutex> _transmitter_mutex;
};

typedef int (QUICNetVConnection::*QUICNetVConnHandler)(int, void *);

extern ClassAllocator<QUICNetVConnection> quicNetVCAllocator;
