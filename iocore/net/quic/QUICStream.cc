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

#include "QUICStream.h"

#include "I_Event.h"
#include "P_VConnection.h"
#include "QUICStreamManager.h"
#include "QUICDebugNames.h"

#define QUICStreamDebug(fmt, ...)                                                                        \
  Debug("quic_stream", "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
        QUICDebugNames::stream_state(this->_state), ##__VA_ARGS__)

#define QUICVStreamDebug(fmt, ...)                                                                         \
  Debug("v_quic_stream", "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
        QUICDebugNames::stream_state(this->_state), ##__VA_ARGS__)

#define QUICStreamFCDebug(fmt, ...)                                                                         \
  Debug("quic_flow_ctrl", "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
        QUICDebugNames::stream_state(this->_state), ##__VA_ARGS__)

static constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD = 24;
static constexpr uint32_t MAX_CRYPTO_FRAME_OVERHEAD = 16;

QUICStream::QUICStream(QUICRTTProvider *rtt_provider, QUICConnectionInfoProvider *cinfo, QUICStreamId sid,
                       uint64_t recv_max_stream_data, uint64_t send_max_stream_data)
  : VConnection(nullptr),
    _connection_info(cinfo),
    _id(sid),
    _remote_flow_controller(send_max_stream_data, _id),
    _local_flow_controller(rtt_provider, recv_max_stream_data, _id),
    _flow_control_buffer_size(recv_max_stream_data),
    _received_stream_frame_buffer(),
    _state(nullptr, &this->_progress_vio, this, nullptr)
{
  SET_HANDLER(&QUICStream::state_stream_open);
  mutex = new_ProxyMutex();

  QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                    this->_local_flow_controller.current_limit());
  QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                    this->_remote_flow_controller.current_limit());
}

QUICStream::~QUICStream()
{
  if (this->_read_event) {
    this->_read_event->cancel();
    this->_read_event = nullptr;
  }

  if (this->_write_event) {
    this->_write_event->cancel();
    this->_write_event = nullptr;
  }
}

QUICStreamId
QUICStream::id() const
{
  return this->_id;
}

const QUICConnectionInfoProvider *
QUICStream::connection_info() const
{
  return this->_connection_info;
}

bool
QUICStream::is_bidirectional() const
{
  return (this->_id & 0x03) < 0x02;
}

QUICOffset
QUICStream::final_offset() const
{
  // TODO Return final offset
  return 0;
}

int
QUICStream::state_stream_open(int event, void *data)
{
  QUICVStreamDebug("%s (%d)", get_vc_event_name(event), event);
  QUICErrorUPtr error = nullptr;

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    int64_t len = this->_process_read_vio();
    if (len > 0) {
      this->_signal_read_event();
    }

    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    int64_t len = this->_process_write_vio();
    if (len > 0) {
      this->_signal_write_event();
    }

    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT: {
    // TODO
    ink_assert(false);
    break;
  }
  default:
    QUICStreamDebug("unknown event");
    ink_assert(false);
  }

  // FIXME error is always nullptr
  if (error != nullptr) {
    if (error->cls == QUICErrorClass::TRANSPORT) {
      QUICStreamDebug("QUICError: %s (%u), %s (0x%x)", QUICDebugNames::error_class(error->cls),
                      static_cast<unsigned int>(error->cls), QUICDebugNames::error_code(error->code),
                      static_cast<unsigned int>(error->code));
    } else {
      QUICStreamDebug("QUICError: %s (%u), APPLICATION ERROR (0x%x)", QUICDebugNames::error_class(error->cls),
                      static_cast<unsigned int>(error->cls), static_cast<unsigned int>(error->code));
    }
    if (dynamic_cast<QUICStreamError *>(error.get()) != nullptr) {
      // Stream Error
      QUICStreamErrorUPtr serror = QUICStreamErrorUPtr(static_cast<QUICStreamError *>(error.get()));
      this->reset(std::move(serror));
    } else {
      // Connection Error
      // TODO Close connection (Does this really happen?)
    }
  }

  return EVENT_DONE;
}

int
QUICStream::state_stream_closed(int event, void *data)
{
  QUICVStreamDebug("%s (%d)", get_vc_event_name(event), event);

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    // ignore
    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    // ignore
    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT: {
    // TODO
    ink_assert(false);
    break;
  }
  default:
    ink_assert(false);
  }

  return EVENT_DONE;
}

// this->_read_vio.nbytes should be INT64_MAX until receive FIN flag
VIO *
QUICStream::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  if (buf) {
    this->_read_vio.buffer.writer_for(buf);
  } else {
    this->_read_vio.buffer.clear();
  }

  this->_read_vio.mutex     = c ? c->mutex : this->mutex;
  this->_read_vio.cont      = c;
  this->_read_vio.nbytes    = nbytes;
  this->_read_vio.ndone     = 0;
  this->_read_vio.vc_server = this;
  this->_read_vio.op        = VIO::READ;

  this->_process_read_vio();
  this->_send_tracked_event(this->_read_event, VC_EVENT_READ_READY, &this->_read_vio);

  return &this->_read_vio;
}

VIO *
QUICStream::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  if (buf) {
    this->_write_vio.buffer.reader_for(buf);
  } else {
    this->_write_vio.buffer.clear();
  }

  this->_write_vio.mutex     = c ? c->mutex : this->mutex;
  this->_write_vio.cont      = c;
  this->_write_vio.nbytes    = nbytes;
  this->_write_vio.ndone     = 0;
  this->_write_vio.vc_server = this;
  this->_write_vio.op        = VIO::WRITE;

  this->_process_write_vio();
  this->_send_tracked_event(this->_write_event, VC_EVENT_WRITE_READY, &this->_write_vio);

  return &this->_write_vio;
}

void
QUICStream::do_io_close(int lerrno)
{
  SET_HANDLER(&QUICStream::state_stream_closed);

  this->_read_vio.buffer.clear();
  this->_read_vio.nbytes = 0;
  this->_read_vio.op     = VIO::NONE;
  this->_read_vio.cont   = nullptr;

  this->_write_vio.buffer.clear();
  this->_write_vio.nbytes = 0;
  this->_write_vio.op     = VIO::NONE;
  this->_write_vio.cont   = nullptr;
}

void
QUICStream::do_io_shutdown(ShutdownHowTo_t howto)
{
  ink_assert(false); // unimplemented yet
  return;
}

void
QUICStream::reenable(VIO *vio)
{
  if (vio->op == VIO::READ) {
    QUICVStreamDebug("read_vio reenabled");

    int64_t len = this->_process_read_vio();
    if (len > 0) {
      this->_signal_read_event();
    }
  } else if (vio->op == VIO::WRITE) {
    QUICVStreamDebug("write_vio reenabled");

    int64_t len = this->_process_write_vio();
    if (len > 0) {
      this->_signal_write_event();
    }
  }
}

void
QUICStream::_write_to_read_vio(QUICOffset offset, const uint8_t *data, uint64_t data_length, bool fin)
{
  SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

  uint64_t bytes_added = this->_read_vio.buffer.writer()->write(data, data_length);

  // Until receive FIN flag, keep nbytes INT64_MAX
  if (fin && bytes_added == data_length) {
    this->_read_vio.nbytes = offset + data_length;
  }
}

void
QUICStream::on_read()
{
  this->_state.update_on_read();
}

void
QUICStream::on_eos()
{
  this->_state.update_on_eos();
}

/**
 * @brief Receive STREAM frame
 * @detail When receive STREAM frame, reorder frames and write to buffer of read_vio.
 * If the reordering or writting operation is heavy, split out them to read function,
 * which is called by application via do_io_read() or reenable().
 */
QUICConnectionErrorUPtr
QUICStream::recv(const QUICStreamFrame &frame)
{
  ink_assert(_id == frame.stream_id());
  ink_assert(this->_read_vio.op == VIO::READ);

  // Check stream state - Do this first before accept the frame
  if (!this->_state.is_allowed_to_receive(frame)) {
    QUICStreamDebug("Canceled receiving %s frame due to the stream state", QUICDebugNames::frame_type(frame.type()));
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::STREAM_STATE_ERROR);
  }

  // Flow Control - Even if it's allowed to receive on the state, it may exceed the limit
  int ret = this->_local_flow_controller.update(frame.offset() + frame.data_length());
  QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                    this->_local_flow_controller.current_limit());
  if (ret != 0) {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::FLOW_CONTROL_ERROR);
  }

  QUICConnectionErrorUPtr error = this->_received_stream_frame_buffer.insert(frame);
  if (error != nullptr) {
    this->_received_stream_frame_buffer.clear();
    return error;
  }

  auto new_frame                   = this->_received_stream_frame_buffer.pop();
  QUICStreamFrameSPtr stream_frame = nullptr;

  while (new_frame != nullptr) {
    stream_frame = std::static_pointer_cast<const QUICStreamFrame>(new_frame);

    this->_write_to_read_vio(stream_frame->offset(), reinterpret_cast<uint8_t *>(stream_frame->data()->start()),
                             stream_frame->data_length(), stream_frame->has_fin_flag());
    this->_state.update_with_receiving_frame(*new_frame);

    new_frame = this->_received_stream_frame_buffer.pop();
  }

  // Forward limit of local flow controller with the largest reordered stream frame
  if (stream_frame) {
    this->_reordered_bytes = stream_frame->offset() + stream_frame->data_length();
    this->_local_flow_controller.forward_limit(this->_reordered_bytes + this->_flow_control_buffer_size);
    QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                      this->_local_flow_controller.current_limit());
  }

  this->_signal_read_event();

  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICMaxStreamDataFrame &frame)
{
  this->_remote_flow_controller.forward_limit(frame.maximum_stream_data());
  QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                    this->_remote_flow_controller.current_limit());

  int64_t len = this->_process_write_vio();
  if (len > 0) {
    this->_signal_write_event();
  }

  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICStreamBlockedFrame &frame)
{
  // STREAM_DATA_BLOCKED frames are for debugging. Nothing to do here.
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICStopSendingFrame &frame)
{
  this->_state.update_with_receiving_frame(frame);
  this->_reset_reason = QUICStreamErrorUPtr(new QUICStreamError(this, QUIC_APP_ERROR_CODE_STOPPING));
  // We received and processed STOP_SENDING frame, so return NO_ERROR here
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICRstStreamFrame &frame)
{
  this->_state.update_with_receiving_frame(frame);
  this->_signal_read_eos_event();
  return nullptr;
}

bool
QUICStream::will_generate_frame(QUICEncryptionLevel level)
{
  return this->_write_vio.get_reader()->read_avail() > 0;
}

QUICFrameUPtr
QUICStream::generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size)
{
  SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

  QUICFrameUPtr frame = this->create_retransmitted_frame(level, maximum_frame_size, this->_issue_frame_id(), this);
  if (frame != nullptr) {
    ink_assert(frame->type() == QUICFrameType::STREAM);
    this->_records_stream_frame(*static_cast<QUICStreamFrame *>(frame.get()));
    return frame;
  }

  // RESET_STREAM
  if (this->_reset_reason && !this->_is_reset_sent) {
    frame = QUICFrameFactory::create_rst_stream_frame(*this->_reset_reason, this->_issue_frame_id(), this);
    this->_records_rst_stream_frame(*static_cast<QUICRstStreamFrame *>(frame.get()));
    this->_state.update_with_sending_frame(*frame);
    this->_is_reset_sent = true;
    return frame;
  }

  // STOP_SENDING
  if (this->_stop_sending_reason && !this->_is_stop_sending_sent) {
    frame =
      QUICFrameFactory::create_stop_sending_frame(this->id(), this->_stop_sending_reason->code, this->_issue_frame_id(), this);
    this->_records_stop_sending_frame(*static_cast<QUICStopSendingFrame *>(frame.get()));
    this->_state.update_with_sending_frame(*frame);
    this->_is_stop_sending_sent = true;
    return frame;
  }

  // MAX_STREAM_DATA
  frame = this->_local_flow_controller.generate_frame(level, UINT16_MAX, maximum_frame_size);
  if (frame) {
    return frame;
  }

  if (!this->_state.is_allowed_to_send(QUICFrameType::STREAM)) {
    return frame;
  }

  uint64_t maximum_data_size = 0;
  if (maximum_frame_size <= MAX_STREAM_FRAME_OVERHEAD) {
    return frame;
  }
  maximum_data_size = maximum_frame_size - MAX_STREAM_FRAME_OVERHEAD;

  bool pure_fin = false;
  bool fin      = false;
  if ((this->_write_vio.nbytes != 0 || this->_write_vio.nbytes != INT64_MAX) &&
      this->_write_vio.nbytes == static_cast<int64_t>(this->_send_offset)) {
    // Pure FIN stream should be sent regardless status of remote flow controller, because the length is zero.
    pure_fin = true;
    fin      = true;
  }

  uint64_t len           = 0;
  IOBufferReader *reader = this->_write_vio.get_reader();
  if (!pure_fin) {
    uint64_t data_len = reader->block_read_avail();
    if (data_len == 0) {
      return frame;
    }

    // Check Connection/Stream level credit only if the generating STREAM frame is not pure fin
    uint64_t stream_credit = this->_remote_flow_controller.credit();
    if (stream_credit == 0) {
      // STREAM_DATA_BLOCKED
      frame = this->_remote_flow_controller.generate_frame(level, UINT16_MAX, maximum_frame_size);
      return frame;
    }

    if (connection_credit == 0) {
      // BLOCKED - BLOCKED frame will be sent by connection level remote flow controller
      return frame;
    }

    len = std::min(data_len, std::min(maximum_data_size, std::min(stream_credit, connection_credit)));

    // data_len, maximum_data_size, stream_credit and connection_credit are already checked they're larger than 0
    ink_assert(len != 0);

    if (this->_write_vio.nbytes == static_cast<int64_t>(this->_send_offset + len)) {
      fin = true;
    }
  }

  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(reader->get_current_block()->clone());
  block->consume(reader->start_offset);
  block->_end = std::min(block->start() + len, block->_buf_end);
  ink_assert(static_cast<uint64_t>(block->read_avail()) == len);

  // STREAM - Pure FIN or data length is lager than 0
  // FIXME has_length_flag and has_offset_flag should be configurable
  frame =
    QUICFrameFactory::create_stream_frame(block, this->_id, this->_send_offset, fin, true, true, this->_issue_frame_id(), this);
  if (!this->_state.is_allowed_to_send(*frame)) {
    QUICStreamDebug("Canceled sending %s frame due to the stream state", QUICDebugNames::frame_type(frame->type()));
    return frame;
  }

  if (!pure_fin) {
    int ret = this->_remote_flow_controller.update(this->_send_offset + len);
    // We cannot cancel sending the frame after updating the flow controller

    // Calling update always success, because len is always less than stream_credit
    ink_assert(ret == 0);

    QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                      this->_remote_flow_controller.current_limit());
    if (this->_remote_flow_controller.current_offset() == this->_remote_flow_controller.current_limit()) {
      QUICStreamDebug("Flow Controller will block sending a STREAM frame");
    }

    reader->consume(len);
    this->_send_offset += len;
    this->_write_vio.ndone += len;
  }
  this->_records_stream_frame(*static_cast<QUICStreamFrame *>(frame.get()));

  this->_signal_write_event();
  this->_state.update_with_sending_frame(*frame);

  return frame;
}

void
QUICStream::_records_stream_frame(const QUICStreamFrame &frame)
{
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = frame.type();
  StreamFrameInfo *frame_info   = reinterpret_cast<StreamFrameInfo *>(info->data);
  frame_info->offset            = frame.offset();
  frame_info->has_fin           = frame.has_fin_flag();
  frame_info->block             = frame.data();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStream::_records_rst_stream_frame(const QUICRstStreamFrame &frame)
{
  QUICFrameInformationUPtr info  = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                     = frame.type();
  RstStreamFrameInfo *frame_info = reinterpret_cast<RstStreamFrameInfo *>(info->data);
  frame_info->error_code         = frame.error_code();
  frame_info->final_offset       = frame.final_offset();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStream::_records_stop_sending_frame(const QUICStopSendingFrame &frame)
{
  QUICFrameInformationUPtr info    = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                       = frame.type();
  StopSendingFrameInfo *frame_info = reinterpret_cast<StopSendingFrameInfo *>(info->data);
  frame_info->error_code           = frame.error_code();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStream::_on_frame_acked(QUICFrameInformationUPtr &info)
{
  StreamFrameInfo *frame_info = nullptr;
  switch (info->type) {
  case QUICFrameType::RESET_STREAM:
    this->_is_reset_complete = true;
    break;
  case QUICFrameType::STREAM:
    frame_info        = reinterpret_cast<StreamFrameInfo *>(info->data);
    frame_info->block = nullptr;
    if (false) {
      this->_is_transfer_complete = true;
    }
    break;
  case QUICFrameType::STOP_SENDING:
  default:
    break;
  }

  this->_state.update_on_ack();
}

void
QUICStream::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  switch (info->type) {
  case QUICFrameType::RESET_STREAM:
    // [draft-16] 13.2.  Retransmission of Information
    // Cancellation of stream transmission, as carried in a RESET_STREAM
    // frame, is sent until acknowledged or until all stream data is
    // acknowledged by the peer (that is, either the "Reset Recvd" or
    // "Data Recvd" state is reached on the send stream).  The content of
    // a RESET_STREAM frame MUST NOT change when it is sent again.
    this->_is_reset_sent = false;
    break;
  case QUICFrameType::STREAM:
    this->save_frame_info(std::move(info));
    break;
  case QUICFrameType::STOP_SENDING:
    this->_is_stop_sending_sent = false;
    break;
  default:
    break;
  }
}

/**
 * Replace existing event only if the new event is different than the inprogress event
 */
Event *
QUICStream::_send_tracked_event(Event *event, int send_event, VIO *vio)
{
  if (event != nullptr) {
    if (event->callback_event != send_event) {
      event->cancel();
      event = nullptr;
    }
  }

  if (event == nullptr) {
    event = this_ethread()->schedule_imm(this, send_event, vio);
  }

  return event;
}

/**
 * @brief Signal event to this->_read_vio.cont
 */
void
QUICStream::_signal_read_event()
{
  if (this->_read_vio.cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return;
  }
  MUTEX_TRY_LOCK(lock, this->_read_vio.mutex, this_ethread());

  int event = this->_read_vio.ntodo() ? VC_EVENT_READ_READY : VC_EVENT_READ_COMPLETE;

  if (lock.is_locked()) {
    this->_read_vio.cont->handleEvent(event, &this->_read_vio);
  } else {
    this_ethread()->schedule_imm(this->_read_vio.cont, event, &this->_read_vio);
  }

  QUICVStreamDebug("%s (%d)", get_vc_event_name(event), event);
}

/**
 * @brief Signal event to this->_write_vio.cont
 */
void
QUICStream::_signal_write_event()
{
  if (this->_write_vio.cont == nullptr || this->_write_vio.op == VIO::NONE) {
    return;
  }
  MUTEX_TRY_LOCK(lock, this->_write_vio.mutex, this_ethread());

  int event = this->_write_vio.ntodo() ? VC_EVENT_WRITE_READY : VC_EVENT_WRITE_COMPLETE;

  if (lock.is_locked()) {
    this->_write_vio.cont->handleEvent(event, &this->_write_vio);
  } else {
    this_ethread()->schedule_imm(this->_write_vio.cont, event, &this->_write_vio);
  }

  QUICVStreamDebug("%s (%d)", get_vc_event_name(event), event);
}

/**
 * @brief Signal event to this->_write_vio.cont
 */
void
QUICStream::_signal_read_eos_event()
{
  if (this->_read_vio.cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return;
  }
  MUTEX_TRY_LOCK(lock, this->_read_vio.mutex, this_ethread());

  int event = VC_EVENT_EOS;

  if (lock.is_locked()) {
    this->_write_vio.cont->handleEvent(event, &this->_write_vio);
  } else {
    this_ethread()->schedule_imm(this->_read_vio.cont, event, &this->_read_vio);
  }

  QUICVStreamDebug("%s (%d)", get_vc_event_name(event), event);
}

int64_t
QUICStream::_process_read_vio()
{
  if (this->_read_vio.cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return 0;
  }

  // Pass through. Read operation is done by QUICStream::recv(const std::shared_ptr<const QUICStreamFrame> frame)
  // TODO: 1. pop frame from _received_stream_frame_buffer
  //       2. write data to _read_vio

  return 0;
}

/**
 * @brief Send STREAM DATA from _response_buffer
 * @detail Call _signal_write_event() to indicate event upper layer
 */
int64_t
QUICStream::_process_write_vio()
{
  if (this->_write_vio.cont == nullptr || this->_write_vio.op == VIO::NONE) {
    return 0;
  }

  return 0;
}

void
QUICStream::stop_sending(QUICStreamErrorUPtr error)
{
  this->_stop_sending_reason = std::move(error);
}

void
QUICStream::reset(QUICStreamErrorUPtr error)
{
  this->_reset_reason = std::move(error);
}

QUICOffset
QUICStream::reordered_bytes() const
{
  return this->_reordered_bytes;
}

QUICOffset
QUICStream::largest_offset_received() const
{
  return this->_local_flow_controller.current_offset();
}

QUICOffset
QUICStream::largest_offset_sent() const
{
  return this->_remote_flow_controller.current_offset();
}

bool
QUICStream::is_transfer_goal_set() const
{
  return this->_received_stream_frame_buffer.is_transfer_goal_set();
}

uint64_t
QUICStream::transfer_progress() const
{
  return this->_received_stream_frame_buffer.transfer_progress();
}

uint64_t
QUICStream::transfer_goal() const
{
  return this->_received_stream_frame_buffer.transfer_goal();
}

bool
QUICStream::is_cancelled() const
{
  return this->_is_reset_complete;
}

//
// QUICCryptoStream
//
QUICCryptoStream::QUICCryptoStream() : _received_stream_frame_buffer()
{
  this->_read_buffer  = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
  this->_write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);

  this->_read_buffer_reader  = this->_read_buffer->alloc_reader();
  this->_write_buffer_reader = this->_write_buffer->alloc_reader();
}

QUICCryptoStream::~QUICCryptoStream()
{
  // All readers will be deallocated
  free_MIOBuffer(this->_read_buffer);
  free_MIOBuffer(this->_write_buffer);
}

/**
 * Reset send/recv offset of stream
 */
void
QUICCryptoStream::reset_send_offset()
{
  this->_send_offset = 0;
}

void
QUICCryptoStream::reset_recv_offset()
{
  this->_received_stream_frame_buffer.clear();
}

QUICConnectionErrorUPtr
QUICCryptoStream::recv(const QUICCryptoFrame &frame)
{
  QUICConnectionErrorUPtr error = this->_received_stream_frame_buffer.insert(frame);
  if (error != nullptr) {
    this->_received_stream_frame_buffer.clear();
    return error;
  }

  auto new_frame = this->_received_stream_frame_buffer.pop();
  while (new_frame != nullptr) {
    QUICCryptoFrameSPtr crypto_frame = std::static_pointer_cast<const QUICCryptoFrame>(new_frame);

    this->_read_buffer->write(reinterpret_cast<uint8_t *>(crypto_frame->data()->start()), crypto_frame->data_length());
    new_frame = this->_received_stream_frame_buffer.pop();
  }

  return nullptr;
}

int64_t
QUICCryptoStream::read_avail()
{
  return this->_read_buffer_reader->read_avail();
}

int64_t
QUICCryptoStream::read(uint8_t *buf, int64_t len)
{
  return this->_read_buffer_reader->read(buf, len);
}

int64_t
QUICCryptoStream::write(const uint8_t *buf, int64_t len)
{
  return this->_write_buffer->write(buf, len);
}

bool
QUICCryptoStream::will_generate_frame(QUICEncryptionLevel level)
{
  return this->_write_buffer_reader->is_read_avail_more_than(0);
}

QUICFrameUPtr
QUICCryptoStream::generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size)
{
  QUICConnectionErrorUPtr error = nullptr;

  if (this->_reset_reason) {
    return QUICFrameFactory::create_rst_stream_frame(*this->_reset_reason);
  }

  QUICFrameUPtr frame = this->create_retransmitted_frame(level, maximum_frame_size, this->_issue_frame_id(), this);
  if (frame != nullptr) {
    ink_assert(frame->type() == QUICFrameType::CRYPTO);
    this->_records_crypto_frame(*static_cast<QUICCryptoFrame *>(frame.get()));
    return frame;
  }

  if (maximum_frame_size <= MAX_CRYPTO_FRAME_OVERHEAD) {
    return frame;
  }

  uint64_t frame_payload_size = maximum_frame_size - MAX_CRYPTO_FRAME_OVERHEAD;
  uint64_t bytes_avail        = this->_write_buffer_reader->read_avail();
  frame_payload_size          = std::min(bytes_avail, frame_payload_size);
  if (frame_payload_size == 0) {
    return frame;
  }

  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(this->_write_buffer_reader->get_current_block()->clone());
  block->consume(this->_write_buffer_reader->start_offset);
  block->_end = std::min(block->start() + frame_payload_size, block->_buf_end);
  ink_assert(static_cast<uint64_t>(block->read_avail()) == frame_payload_size);

  frame = QUICFrameFactory::create_crypto_frame(block, this->_send_offset, this->_issue_frame_id(), this);
  this->_send_offset += frame_payload_size;
  this->_write_buffer_reader->consume(frame_payload_size);
  this->_records_crypto_frame(*static_cast<QUICCryptoFrame *>(frame.get()));

  return frame;
}

void
QUICCryptoStream::_on_frame_acked(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::CRYPTO);
  CryptoFrameInfo *crypto_frame_info = reinterpret_cast<CryptoFrameInfo *>(info->data);
  crypto_frame_info->block           = nullptr;
}

void
QUICCryptoStream::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::CRYPTO);
  this->save_frame_info(std::move(info));
}

void
QUICCryptoStream::_records_crypto_frame(const QUICCryptoFrame &frame)
{
  QUICFrameInformationUPtr info      = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                         = QUICFrameType::CRYPTO;
  CryptoFrameInfo *crypto_frame_info = reinterpret_cast<CryptoFrameInfo *>(info->data);
  crypto_frame_info->offset          = frame.offset();
  crypto_frame_info->block           = frame.data();
  this->_records_frame(frame.id(), std::move(info));
}
