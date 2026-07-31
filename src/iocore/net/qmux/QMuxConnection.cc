/** @file

  QMux connection implementation wrapping quiche_conn (draft-opik-quic-qmux-01)

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

#include "iocore/net/qmux/QMuxConnection.h"
#include "iocore/net/NetVConnection.h"
#include "iocore/net/quic/QUICContext.h"
#include "iocore/net/quic/QUICApplicationMap.h"
#include "iocore/net/quic/QUICStreamManager.h"
#include "iocore/eventsystem/IOBuffer.h"
#include "iocore/eventsystem/VIO.h"
#include "tscore/Diags.h"
#include "tsutil/DbgCtl.h"

#include <quiche.h>
#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace
{
DbgCtl        dbg_ctl_qmux{"qmux"};
constexpr int QMUX_IO_BUFFER_SIZE_INDEX = BUFFER_SIZE_INDEX_32K;

// Largest Frames field we advertise via qmux_max_record_size. quiche rejects
// anything smaller than this, and enforces the limit on records the peer sends.
constexpr uint64_t QMUX_MAX_RECORD_SIZE = 16382;

// A record is Size (varint, at most 8 bytes) followed by Frames, so this bounds
// the bytes that must be contiguous for quiche to parse one record.
constexpr int64_t QMUX_MAX_RECORD_BYTES = QMUX_MAX_RECORD_SIZE + 8;

// Staging size for records handed to the transport on each send.
constexpr int64_t QMUX_SEND_BUFFER_SIZE = 65535;

constexpr QUICVersion QMUX_QUIC_VERSION = 0x00000001;

std::once_flag qmux_shared_config_once;
} // end anonymous namespace

quiche_config *QMuxConnection::_shared_config = nullptr;

void
QMuxConnection::_init_shared_config()
{
  std::call_once(qmux_shared_config_once, []() {
    quiche_config *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (config == nullptr) {
      Error("failed to create a QMux config");
      return;
    }

    std::string alpn("\x07h3qx-01");
    quiche_config_set_application_protos(config, reinterpret_cast<const uint8_t *>(alpn.c_str()), alpn.size());

    quiche_config_set_max_idle_timeout(config, 30000);
    quiche_config_set_initial_max_data(config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 1000000);
    quiche_config_set_initial_max_stream_data_uni(config, 1000000);
    quiche_config_set_initial_max_streams_bidi(config, 100);
    quiche_config_set_initial_max_streams_uni(config, 100);
    quiche_config_set_disable_active_migration(config, true);

    quiche_config_enable_qmux(config, true);
    quiche_config_set_qmux_max_record_size(config, QMUX_MAX_RECORD_SIZE);

    _shared_config = config;
  });
}

QMuxConnection::QMuxConnection(NetVConnection *netvc) : Continuation(netvc->mutex)
{
  _init_shared_config();
  SET_HANDLER(&QMuxConnection::main_event);

  _synthetic_cid.randomize();
  _cids_str = _synthetic_cid.hex();

  auto *local_ep = netvc->get_local_addr();
  auto *peer_ep  = netvc->get_remote_addr();

  _local_addr_len = ats_ip_size(local_ep);
  _peer_addr_len  = ats_ip_size(peer_ep);
  memcpy(&_local_addr, local_ep, _local_addr_len);
  memcpy(&_peer_addr, peer_ep, _peer_addr_len);

  if (_shared_config != nullptr) {
    _quiche_con =
      quiche_accept(_synthetic_cid, _synthetic_cid.length(), nullptr, 0, reinterpret_cast<const sockaddr *>(&_local_addr),
                    _local_addr_len, reinterpret_cast<const sockaddr *>(&_peer_addr), _peer_addr_len, _shared_config);
  }
  if (_quiche_con == nullptr) {
    Error("failed to create a QMux connection");
    _closed = true;
  }

  _context        = std::make_unique<QUICContext>(this);
  _app_map        = std::make_unique<QUICApplicationMap>();
  _stream_manager = std::make_unique<QUICStreamManager>(_context.get(), _app_map.get());
}

QMuxConnection::~QMuxConnection()
{
  if (_read_reader) {
    _read_reader->dealloc();
  }
  if (_read_buf) {
    free_MIOBuffer(_read_buf);
  }
  if (_write_buf) {
    free_MIOBuffer(_write_buf);
  }
  if (_quiche_con != nullptr) {
    quiche_conn_free(_quiche_con);
    _quiche_con = nullptr;
  }
}

void
QMuxConnection::start(NetVConnection *netvc)
{
  _read_buf    = new_MIOBuffer(QMUX_IO_BUFFER_SIZE_INDEX);
  _read_reader = _read_buf->alloc_reader();
  _write_buf   = new_MIOBuffer(QMUX_IO_BUFFER_SIZE_INDEX);

  netvc->do_io_read(this, INT64_MAX, _read_buf);
  _write_vio = netvc->do_io_write(this, INT64_MAX, _write_buf->alloc_reader());
}

void
QMuxConnection::signal_write_ready()
{
  if (_in_write) {
    return;
  }
  if (_write_vio) {
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    _write_vio->reenable();
  }
}

int
QMuxConnection::main_event(int event, void * /* data ATS_UNUSED */)
{
  if (_quiche_con == nullptr) {
    return EVENT_DONE;
  }

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    _handle_read();
    break;
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    _handle_write();
    break;
  case EVENT_INTERVAL:
    quiche_conn_on_timeout(_quiche_con);
    _flush_quiche_output();
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    Dbg(dbg_ctl_qmux, "connection event %d, closing", event);
    close_quic_connection(nullptr);
    break;
  default:
    break;
  }

  return EVENT_CONT;
}

void
QMuxConnection::_handle_read()
{
  if (_read_reader->read_avail() <= 0) {
    return;
  }

  // quiche parses at most one record per call and needs it in contiguous
  // memory. A record can straddle IOBufferBlock boundaries, so the spanning
  // case is staged through this buffer; the common case reads in place.
  uint8_t staging[QMUX_MAX_RECORD_BYTES];

  while (_read_reader->read_avail() > 0) {
    int64_t avail   = _read_reader->read_avail();
    int64_t blk_len = _read_reader->block_read_avail();

    if (blk_len <= 0) {
      _read_reader->skip_empty_blocks();
      continue;
    }

    uint8_t *buf = nullptr;
    int64_t  len = 0;

    if (blk_len == avail) {
      buf = reinterpret_cast<uint8_t *>(_read_reader->start());
      len = blk_len;
    } else {
      len = std::min(avail, QMUX_MAX_RECORD_BYTES);
      _read_reader->memcpy(staging, len, 0);
      buf = staging;
    }

    quiche_recv_info recv_info = {};
    recv_info.from             = const_cast<sockaddr *>(reinterpret_cast<const sockaddr *>(&_peer_addr));
    recv_info.from_len         = _peer_addr_len;
    recv_info.to               = const_cast<sockaddr *>(reinterpret_cast<const sockaddr *>(&_local_addr));
    recv_info.to_len           = _local_addr_len;

    ssize_t done = quiche_conn_recv(_quiche_con, buf, len, &recv_info);
    if (done < 0) {
      // The record is incomplete. Leave the bytes for the next read event.
      if (done != QUICHE_ERR_DONE) {
        Dbg(dbg_ctl_qmux, "quiche_conn_recv error: %zd", done);
      }
      break;
    }
    _read_reader->consume(done);
  }

  _handle_read_streams();
  _handle_write();
}

void
QMuxConnection::_handle_read_streams()
{
  quiche_stream_iter *readable = quiche_conn_readable(_quiche_con);
  uint64_t            stream_id;

  while (quiche_stream_iter_next(readable, &stream_id)) {
    QUICStream *stream = _stream_manager->find_stream(stream_id);
    if (stream == nullptr) {
      QUICConnectionError err;
      stream = _stream_manager->create_stream(stream_id, err);
      if (stream == nullptr) {
        Dbg(dbg_ctl_qmux, "failed to create stream %" PRIu64, stream_id);
        continue;
      }
    }
    stream->receive_data(*this);
  }
  quiche_stream_iter_free(readable);
}

void
QMuxConnection::_handle_write()
{
  _in_write = true;
  _handle_write_streams();
  _flush_quiche_output();
  _in_write = false;
}

void
QMuxConnection::_flush_quiche_output()
{
  bool             wrote = false;
  uint8_t          out[QMUX_SEND_BUFFER_SIZE];
  quiche_send_info send_info;

  for (;;) {
    ssize_t written = quiche_conn_send(_quiche_con, out, sizeof(out), &send_info);
    if (written == QUICHE_ERR_DONE) {
      break;
    }
    if (written < 0) {
      Dbg(dbg_ctl_qmux, "quiche_conn_send error: %zd", written);
      break;
    }
    _write_buf->write(out, written);
    wrote = true;
  }

  if (wrote && _write_vio) {
    _write_vio->reenable();
  }
}

void
QMuxConnection::_handle_write_streams()
{
  if (!quiche_conn_is_established(_quiche_con)) {
    return;
  }

  quiche_stream_iter *writable = quiche_conn_writable(_quiche_con);
  uint64_t            stream_id;

  while (quiche_stream_iter_next(writable, &stream_id)) {
    QUICStream *stream = _stream_manager->find_stream(stream_id);
    if (stream != nullptr) {
      stream->send_data(*this);
    }
  }
  quiche_stream_iter_free(writable);
}

// --- QUICConnectionInfoProvider ---

QUICConnectionId
QMuxConnection::peer_connection_id() const
{
  return QUICConnectionId::ZERO();
}

QUICConnectionId
QMuxConnection::original_connection_id() const
{
  return QUICConnectionId::ZERO();
}

QUICConnectionId
QMuxConnection::first_connection_id() const
{
  return _synthetic_cid;
}

QUICConnectionId
QMuxConnection::retry_source_connection_id() const
{
  return QUICConnectionId::ZERO();
}

QUICConnectionId
QMuxConnection::initial_source_connection_id() const
{
  return _synthetic_cid;
}

QUICConnectionId
QMuxConnection::connection_id() const
{
  return _synthetic_cid;
}

std::string_view
QMuxConnection::cids() const
{
  return _cids_str;
}

const QUICFiveTuple
QMuxConnection::five_tuple() const
{
  return QUICFiveTuple();
}

uint32_t
QMuxConnection::pmtu() const
{
  // Not meaningful over TCP.
  return QMUX_SEND_BUFFER_SIZE;
}

NetVConnectionContext_t
QMuxConnection::direction() const
{
  return NET_VCONNECTION_IN;
}

bool
QMuxConnection::is_closed() const
{
  return _closed;
}

bool
QMuxConnection::is_at_anti_amplification_limit() const
{
  return false;
}

bool
QMuxConnection::is_address_validation_completed() const
{
  return true;
}

bool
QMuxConnection::is_handshake_completed() const
{
  return true;
}

QUICVersion
QMuxConnection::negotiated_version() const
{
  return QMUX_QUIC_VERSION;
}

std::string_view
QMuxConnection::negotiated_application_name() const
{
  return "h3qx-01";
}

void
QMuxConnection::on_stream_updated()
{
  this->signal_write_ready();
}

// --- QUICStreamIO ---

int64_t
QMuxConnection::read_stream(QUICStreamId stream_id, uint8_t *buf, size_t len, bool &fin, QUICStreamIO::ErrorCode &error_code)
{
  return quiche_conn_stream_recv(_quiche_con, stream_id, buf, len, &fin, &error_code);
}

bool
QMuxConnection::stream_read_finished(QUICStreamId stream_id)
{
  return quiche_conn_stream_finished(_quiche_con, stream_id);
}

int64_t
QMuxConnection::stream_write_capacity(QUICStreamId stream_id)
{
  return quiche_conn_stream_capacity(_quiche_con, stream_id);
}

int64_t
QMuxConnection::write_stream(QUICStreamId stream_id, uint8_t const *buf, size_t len, bool fin, QUICStreamIO::ErrorCode &error_code)
{
  return quiche_conn_stream_send(_quiche_con, stream_id, const_cast<uint8_t *>(buf), len, fin, &error_code);
}

// --- QUICConnection ---

QUICStreamManager *
QMuxConnection::stream_manager()
{
  return _stream_manager.get();
}

void
QMuxConnection::close_quic_connection(QUICConnectionErrorUPtr error)
{
  if (_closed) {
    return;
  }
  _closed = true;

  uint64_t err_code = 0;
  if (error) {
    err_code = error->code;
  }

  if (int rv = quiche_conn_close(_quiche_con, true, err_code, nullptr, 0); rv < 0) {
    Dbg(dbg_ctl_qmux, "[%s] quiche_conn_close error: %d", _cids_str.c_str(), rv);
  }
  Dbg(dbg_ctl_qmux, "[%s] connection closed with error %" PRIu64, _cids_str.c_str(), err_code);
}

void
QMuxConnection::reset_quic_connection()
{
  _closed = true;
}

void
QMuxConnection::handle_received_packet(UDPPacket * /* packet ATS_UNUSED */)
{
}

void
QMuxConnection::ping()
{
}
