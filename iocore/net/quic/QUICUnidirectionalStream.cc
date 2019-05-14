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

#include "QUICUnidirectionalStream.h"

//
// QUICSendStream
//
QUICSendStream::QUICSendStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid, uint64_t send_max_stream_data)
  : QUICStreamVConnection(cinfo, sid), _remote_flow_controller(send_max_stream_data, _id), _state(nullptr, &this->_progress_vio)
{
  SET_HANDLER(&QUICSendStream::state_stream_open);

  QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                    this->_remote_flow_controller.current_limit());
}

int
QUICSendStream::state_stream_open(int event, void *data)
{
  QUICVStreamDebug("%s (%d)", get_vc_event_name(event), event);
  QUICErrorUPtr error = nullptr;

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    // should not schedule read event.
    ink_assert(0);
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
QUICSendStream::state_stream_closed(int event, void *data)
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

bool
QUICSendStream::will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp)
{
  return !this->is_retransmited_frame_queue_empty() || this->_write_vio.get_reader()->is_read_avail_more_than(0);
}

QUICFrame *
QUICSendStream::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                               ink_hrtime timestamp)
{
  SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

  QUICFrame *frame = this->create_retransmitted_frame(buf, level, maximum_frame_size, this->_issue_frame_id(), this);
  if (frame != nullptr) {
    ink_assert(frame->type() == QUICFrameType::STREAM);
    this->_records_stream_frame(level, *static_cast<QUICStreamFrame *>(frame));
    return frame;
  }

  // RESET_STREAM
  if (this->_reset_reason && !this->_is_reset_sent) {
    frame = QUICFrameFactory::create_rst_stream_frame(buf, *this->_reset_reason, this->_issue_frame_id(), this);
    this->_records_rst_stream_frame(level, *static_cast<QUICRstStreamFrame *>(frame));
    this->_state.update_with_sending_frame(*frame);
    this->_is_reset_sent = true;
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
      frame = this->_remote_flow_controller.generate_frame(buf, level, UINT16_MAX, maximum_frame_size, timestamp);
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
  frame = QUICFrameFactory::create_stream_frame(buf, block, this->_id, this->_send_offset, fin, true, true, this->_issue_frame_id(),
                                                this);
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
  this->_records_stream_frame(level, *static_cast<QUICStreamFrame *>(frame));

  this->_signal_write_event();
  this->_state.update_with_sending_frame(*frame);

  return frame;
}

QUICConnectionErrorUPtr
QUICSendStream::recv(const QUICStopSendingFrame &frame)
{
  this->_state.update_with_receiving_frame(frame);
  this->reset(QUICStreamErrorUPtr(new QUICStreamError(this, QUIC_APP_ERROR_CODE_STOPPING)));
  // We received and processed STOP_SENDING frame, so return NO_ERROR here
  return nullptr;
}

QUICConnectionErrorUPtr
QUICSendStream::recv(const QUICMaxStreamDataFrame &frame)
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

VIO *
QUICSendStream::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  QUICStreamDebug("Warning wants to read from send only stream ignore");
  // FIXME: should not assert here
  ink_assert(!"read from send only stream");
  return nullptr;
}

VIO *
QUICSendStream::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
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
QUICSendStream::do_io_close(int lerrno)
{
  SET_HANDLER(&QUICSendStream::state_stream_closed);

  ink_assert(this->_read_vio.nbytes == 0);
  ink_assert(this->_read_vio.op == VIO::NONE);
  ink_assert(this->_read_vio.cont == nullptr);
  this->_read_vio.buffer.clear();

  this->_write_vio.buffer.clear();
  this->_write_vio.nbytes = 0;
  this->_write_vio.op     = VIO::NONE;
  this->_write_vio.cont   = nullptr;
}

void
QUICSendStream::do_io_shutdown(ShutdownHowTo_t howto)
{
  switch (howto) {
  case IO_SHUTDOWN_READ:
    // ignore
    break;
  case IO_SHUTDOWN_WRITE:
  case IO_SHUTDOWN_READWRITE:
    this->do_io_close();
    break;
  default:
    ink_assert(0);
    break;
  }
}

void
QUICSendStream::reenable(VIO *vio)
{
  ink_assert(vio == &this->_write_vio);
  ink_assert(vio->op == VIO::WRITE);

  int64_t len = this->_process_write_vio();
  if (len > 0) {
    this->_signal_write_event();
  }
}

void
QUICSendStream::reset(QUICStreamErrorUPtr error)
{
  this->_reset_reason = std::move(error);
}

void
QUICSendStream::_on_frame_acked(QUICFrameInformationUPtr &info)
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
  default:
    ink_assert(!"unexpected frame type");
    break;
  }
}

void
QUICSendStream::_on_frame_lost(QUICFrameInformationUPtr &info)
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
  default:
    ink_assert(!"unexpected frame type");
    break;
  }
}

QUICOffset
QUICSendStream::largest_offset_sent() const
{
  return this->_remote_flow_controller.current_offset();
}

//
// QUICReceiveStream
//
QUICReceiveStream::QUICReceiveStream(QUICRTTProvider *rtt_provider, QUICConnectionInfoProvider *cinfo, QUICStreamId sid,
                                     uint64_t recv_max_stream_data)
  : QUICStreamVConnection(cinfo, sid),
    _local_flow_controller(rtt_provider, recv_max_stream_data, _id),
    _flow_control_buffer_size(recv_max_stream_data),
    _state(this, nullptr)
{
  SET_HANDLER(&QUICReceiveStream::state_stream_open);

  QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                    this->_local_flow_controller.current_limit());
}

int
QUICReceiveStream::state_stream_open(int event, void *data)
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
    // should not schedule write event
    ink_assert(!"should not schedule write even");
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
QUICReceiveStream::state_stream_closed(int event, void *data)
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

bool
QUICReceiveStream::is_transfer_goal_set() const
{
  return this->_received_stream_frame_buffer.is_transfer_goal_set();
}

uint64_t
QUICReceiveStream::transfer_progress() const
{
  return this->_received_stream_frame_buffer.transfer_progress();
}

uint64_t
QUICReceiveStream::transfer_goal() const
{
  return this->_received_stream_frame_buffer.transfer_goal();
}

bool
QUICReceiveStream::is_cancelled() const
{
  return this->_is_stop_sending_complete;
}

bool
QUICReceiveStream::will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp)
{
  return this->_local_flow_controller.will_generate_frame(level, timestamp) ||
         (this->_stop_sending_reason != nullptr && this->_is_stop_sending_sent == false);
}

QUICFrame *
QUICReceiveStream::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                                  ink_hrtime timestamp)
{
  QUICFrame *frame = nullptr;
  // STOP_SENDING
  if (this->_stop_sending_reason && !this->_is_stop_sending_sent) {
    frame =
      QUICFrameFactory::create_stop_sending_frame(buf, this->id(), this->_stop_sending_reason->code, this->_issue_frame_id(), this);
    this->_records_stop_sending_frame(level, *static_cast<QUICStopSendingFrame *>(frame));
    this->_state.update_with_sending_frame(*frame);
    this->_is_stop_sending_sent = true;
    return frame;
  }

  // MAX_STREAM_DATA
  frame = this->_local_flow_controller.generate_frame(buf, level, UINT16_MAX, maximum_frame_size, timestamp);
  return frame;
}

QUICConnectionErrorUPtr
QUICReceiveStream::recv(const QUICRstStreamFrame &frame)
{
  this->_state.update_with_receiving_frame(frame);
  this->_signal_read_eos_event();
  return nullptr;
}

QUICConnectionErrorUPtr
QUICReceiveStream::recv(const QUICStreamDataBlockedFrame &frame)
{
  // STREAM_DATA_BLOCKED frames are for debugging. Nothing to do here.
  QUICStreamFCDebug("[REMOTE] blocked %" PRIu64, frame.offset());
  return nullptr;
}

/**
 * @brief Receive STREAM frame
 * @detail When receive STREAM frame, reorder frames and write to buffer of read_vio.
 * If the reordering or writting operation is heavy, split out them to read function,
 * which is called by application via do_io_read() or reenable().
 */
QUICConnectionErrorUPtr
QUICReceiveStream::recv(const QUICStreamFrame &frame)
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

  // Make a copy and insert it into the receive buffer because the frame passed is temporal
  QUICFrame *cloned             = new QUICStreamFrame(frame);
  QUICConnectionErrorUPtr error = this->_received_stream_frame_buffer.insert(cloned);
  if (error != nullptr) {
    this->_received_stream_frame_buffer.clear();
    return error;
  }

  auto new_frame                      = this->_received_stream_frame_buffer.pop();
  const QUICStreamFrame *stream_frame = nullptr;
  uint64_t last_offset                = 0;
  uint64_t last_length                = 0;

  while (new_frame != nullptr) {
    stream_frame = static_cast<const QUICStreamFrame *>(new_frame);
    last_offset  = stream_frame->offset();
    last_length  = stream_frame->data_length();

    this->_write_to_read_vio(stream_frame->offset(), reinterpret_cast<uint8_t *>(stream_frame->data()->start()),
                             stream_frame->data_length(), stream_frame->has_fin_flag());
    this->_state.update_with_receiving_frame(*new_frame);

    delete new_frame;
    new_frame = this->_received_stream_frame_buffer.pop();
  }

  // Forward limit of local flow controller with the largest reordered stream frame
  if (stream_frame) {
    this->_reordered_bytes = last_offset + last_length;
    this->_local_flow_controller.forward_limit(this->_reordered_bytes + this->_flow_control_buffer_size);
    QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                      this->_local_flow_controller.current_limit());
  }

  this->_signal_read_event();

  return nullptr;
}

VIO *
QUICReceiveStream::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
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
QUICReceiveStream::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  QUICStreamDebug("Warning wants to write to send only stream ignore");
  // FIXME: should not assert here
  ink_assert(!"write to send only stream");
  return nullptr;
}

void
QUICReceiveStream::do_io_close(int lerrno)
{
  SET_HANDLER(&QUICReceiveStream::state_stream_closed);

  ink_assert(this->_write_vio.nbytes == 0);
  ink_assert(this->_write_vio.op == VIO::NONE);
  ink_assert(this->_write_vio.cont == nullptr);
  this->_write_vio.buffer.clear();

  this->_read_vio.buffer.clear();
  this->_read_vio.nbytes = 0;
  this->_read_vio.op     = VIO::NONE;
  this->_read_vio.cont   = nullptr;
}

void
QUICReceiveStream::do_io_shutdown(ShutdownHowTo_t howto)
{
  switch (howto) {
  case IO_SHUTDOWN_WRITE:
    // ignore
    break;
  case IO_SHUTDOWN_READ:
  case IO_SHUTDOWN_READWRITE:
    this->do_io_close();
    break;
  default:
    ink_assert(0);
    break;
  }
}

void
QUICReceiveStream::reenable(VIO *vio)
{
  ink_assert(vio == &this->_read_vio);
  ink_assert(vio->op == VIO::READ);

  int64_t len = this->_process_read_vio();
  if (len > 0) {
    this->_signal_read_event();
  }
}

void
QUICReceiveStream::on_read()
{
  this->_state.update_on_read();
}

void
QUICReceiveStream::on_eos()
{
  this->_state.update_on_eos();
}

QUICOffset
QUICReceiveStream::largest_offset_received() const
{
  return this->_local_flow_controller.current_offset();
}

void
QUICReceiveStream::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  switch (info->type) {
  case QUICFrameType::STOP_SENDING:
    this->_is_stop_sending_sent = false;
    break;
  default:
    ink_assert(!"unknown frame type");
    break;
  }
}

void
QUICReceiveStream::_on_frame_acked(QUICFrameInformationUPtr &info)
{
  switch (info->type) {
  case QUICFrameType::STOP_SENDING:
    this->_is_stop_sending_complete = true;
    break;
  default:
    ink_assert(!"unknown frame type");
    break;
  }
}

void
QUICReceiveStream::stop_sending(QUICStreamErrorUPtr error)
{
  this->_stop_sending_reason = std::move(error);
}
