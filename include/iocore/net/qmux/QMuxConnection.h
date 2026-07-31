/** @file

  QMux connection wrapping quiche_conn (draft-opik-quic-qmux-01)

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

#include "iocore/net/quic/QUICConnection.h"
#include "iocore/net/quic/QUICStream.h"
#include "iocore/eventsystem/Continuation.h"
#include "tscore/ink_inet.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct quiche_conn;
struct quiche_config;

class NetVConnection;
class VIO;
class MIOBuffer;
class IOBufferReader;
class QUICContext;
class QUICApplicationMap;
class QUICStreamManager;

/**
 * QMux connection implementing QUICConnection interface.
 * Wraps a quiche_conn* created with QMux-enabled config.
 * Also acts as the I/O event handler (Continuation) that bridges
 * the SSLNetVConnection byte stream to quiche framing, and as the
 * QUICStreamIO backend that QUICStream uses to move stream data.
 */
class QMuxConnection : public QUICConnection, public Continuation, public QUICStreamIO
{
public:
  explicit QMuxConnection(NetVConnection *netvc);
  ~QMuxConnection() override;

  // QUICConnectionInfoProvider
  QUICConnectionId        peer_connection_id() const override;
  QUICConnectionId        original_connection_id() const override;
  QUICConnectionId        first_connection_id() const override;
  QUICConnectionId        retry_source_connection_id() const override;
  QUICConnectionId        initial_source_connection_id() const override;
  QUICConnectionId        connection_id() const override;
  std::string_view        cids() const override;
  const QUICFiveTuple     five_tuple() const override;
  uint32_t                pmtu() const override;
  NetVConnectionContext_t direction() const override;
  bool                    is_closed() const override;
  bool                    is_at_anti_amplification_limit() const override;
  bool                    is_address_validation_completed() const override;
  bool                    is_handshake_completed() const override;
  QUICVersion             negotiated_version() const override;
  std::string_view        negotiated_application_name() const override;
  void                    on_stream_updated() override;

  // QUICStreamIO
  int64_t read_stream(QUICStreamId stream_id, uint8_t *buf, size_t len, bool &fin, QUICStreamIO::ErrorCode &error_code) override;
  bool    stream_read_finished(QUICStreamId stream_id) override;
  int64_t stream_write_capacity(QUICStreamId stream_id) override;
  int64_t write_stream(QUICStreamId stream_id, uint8_t const *buf, size_t len, bool fin,
                       QUICStreamIO::ErrorCode &error_code) override;

  // QUICConnection
  QUICStreamManager *stream_manager() override;
  void               close_quic_connection(QUICConnectionErrorUPtr error) override;
  void               reset_quic_connection() override;
  void               handle_received_packet(UDPPacket *packet) override;
  void               ping() override;

  void start(NetVConnection *netvc);
  void signal_write_ready();

private:
  int  main_event(int event, void *data);
  void _handle_read();
  void _handle_write();
  void _flush_quiche_output();
  void _handle_read_streams();
  void _handle_write_streams();

  static quiche_config *_shared_config;
  static void           _init_shared_config();

  quiche_conn *_quiche_con = nullptr;

  sockaddr_storage _local_addr     = {};
  socklen_t        _local_addr_len = 0;
  sockaddr_storage _peer_addr      = {};
  socklen_t        _peer_addr_len  = 0;

  std::unique_ptr<QUICApplicationMap> _app_map;
  std::unique_ptr<QUICContext>        _context;
  std::unique_ptr<QUICStreamManager>  _stream_manager;

  QUICConnectionId _synthetic_cid;
  std::string      _cids_str;

  bool _closed   = false;
  bool _in_write = false;

  MIOBuffer      *_read_buf    = nullptr;
  IOBufferReader *_read_reader = nullptr;
  MIOBuffer      *_write_buf   = nullptr;
  VIO            *_write_vio   = nullptr;
};
