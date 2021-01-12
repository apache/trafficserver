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

#include "QUICBidirectionalStream.h"
#include "QUICStreamAdapter.h"

//
// QUICBidirectionalStream
//
QUICBidirectionalStream::QUICBidirectionalStream(QUICRTTProvider *rtt_provider, QUICConnectionInfoProvider *cinfo, QUICStreamId sid,
                                                 uint64_t recv_max_stream_data, uint64_t send_max_stream_data)
  : QUICStream(cinfo, sid),
    _remote_flow_controller(send_max_stream_data, _id),
    _local_flow_controller(rtt_provider, recv_max_stream_data, _id),
    _flow_control_buffer_size(recv_max_stream_data),
    _state(nullptr, &this->_progress_sa, this, nullptr)
{
  QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                    this->_local_flow_controller.current_limit());
  QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                    this->_remote_flow_controller.current_limit());
}

bool
QUICBidirectionalStream::is_transfer_goal_set() const
{
  return this->_received_stream_frame_buffer.is_transfer_goal_set();
}

uint64_t
QUICBidirectionalStream::transfer_progress() const
{
  return this->_received_stream_frame_buffer.transfer_progress();
}

uint64_t
QUICBidirectionalStream::transfer_goal() const
{
  return this->_received_stream_frame_buffer.transfer_goal();
}

bool
QUICBidirectionalStream::is_cancelled() const
{
  return this->_is_reset_complete;
}

/**
 * @brief Receive STREAM frame
 * @detail When receive STREAM frame, reorder frames and write to buffer of read_vio.
 * If the reordering or writing operation is heavy, split out them to read function,
 * which is called by application via do_io_read() or reenable().
 */
QUICConnectionErrorUPtr
QUICBidirectionalStream::recv(const QUICStreamFrame &frame)
{
  ink_assert(_id == frame.stream_id());

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

    this->_adapter->write(stream_frame->offset(), reinterpret_cast<uint8_t *>(stream_frame->data()->start()),
                          stream_frame->data_length(), stream_frame->has_fin_flag());
    if (this->_state.update_with_receiving_frame(*new_frame)) {
      this->_notify_state_change();
    }

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

  this->_adapter->encourge_read();

  return nullptr;
}

QUICConnectionErrorUPtr
QUICBidirectionalStream::recv(const QUICMaxStreamDataFrame &frame)
{
  this->_remote_flow_controller.forward_limit(frame.maximum_stream_data());
  QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                    this->_remote_flow_controller.current_limit());

  this->_adapter->encourge_write();

  return nullptr;
}

QUICConnectionErrorUPtr
QUICBidirectionalStream::recv(const QUICStreamDataBlockedFrame &frame)
{
  // STREAM_DATA_BLOCKED frames are for debugging. Nothing to do here.
  QUICStreamFCDebug("[REMOTE] blocked %" PRIu64, frame.offset());
  return nullptr;
}

QUICConnectionErrorUPtr
QUICBidirectionalStream::recv(const QUICStopSendingFrame &frame)
{
  if (this->_state.update_with_receiving_frame(frame)) {
    this->_notify_state_change();
  }
  this->_reset_reason = QUICStreamErrorUPtr(new QUICStreamError(this, QUIC_APP_ERROR_CODE_STOPPING));
  // We received and processed STOP_SENDING frame, so return NO_ERROR here
  return nullptr;
}

QUICConnectionErrorUPtr
QUICBidirectionalStream::recv(const QUICRstStreamFrame &frame)
{
  if (this->_state.update_with_receiving_frame(frame)) {
    this->_notify_state_change();
  }
  this->_adapter->notify_eos();
  return nullptr;
}

bool
QUICBidirectionalStream::will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting,
                                             uint32_t seq_num)
{
  if (this->_local_flow_controller.will_generate_frame(level, current_packet_size, ack_eliciting, seq_num)) {
    return true;
  }
  if (!this->is_retransmited_frame_queue_empty()) {
    return true;
  }
  if (this->_adapter && this->_adapter->unread_len() > 0) {
    return true;
  }
  return false;
}

QUICFrame *
QUICBidirectionalStream::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit,
                                        uint16_t maximum_frame_size, size_t current_packet_size, uint32_t seq_num)
{
  QUICFrame *frame = this->create_retransmitted_frame(buf, level, maximum_frame_size, this->_issue_frame_id(), this);
  if (frame != nullptr) {
    ink_assert(frame->type() == QUICFrameType::STREAM);
    this->_records_stream_frame(level, *static_cast<QUICStreamFrame *>(frame));
    return frame;
  }

  // RESET_STREAM
  if (this->_reset_reason && !this->_is_reset_sent) {
    frame = QUICFrameFactory::create_rst_stream_frame(buf, *this->_reset_reason, this->_issue_frame_id(), this);
    if (frame->size() > maximum_frame_size) {
      frame->~QUICFrame();
      return nullptr;
    }
    this->_records_rst_stream_frame(level, *static_cast<QUICRstStreamFrame *>(frame));
    if (this->_state.update_with_sending_frame(*frame)) {
      this->_notify_state_change();
    }
    this->_is_reset_sent = true;
    return frame;
  }

  // STOP_SENDING
  if (this->_stop_sending_reason && !this->_is_stop_sending_sent) {
    frame =
      QUICFrameFactory::create_stop_sending_frame(buf, this->id(), this->_stop_sending_reason->code, this->_issue_frame_id(), this);
    if (frame->size() > maximum_frame_size) {
      frame->~QUICFrame();
      return nullptr;
    }
    this->_records_stop_sending_frame(level, *static_cast<QUICStopSendingFrame *>(frame));
    if (this->_state.update_with_sending_frame(*frame)) {
      this->_notify_state_change();
    }
    this->_is_stop_sending_sent = true;
    return frame;
  }

  // MAX_STREAM_DATA
  frame = this->_local_flow_controller.generate_frame(buf, level, UINT16_MAX, maximum_frame_size, current_packet_size, seq_num);
  if (frame) {
    // maximum_frame_size should be checked in QUICFlowController
    return frame;
  }

  if (!this->_adapter || !this->_state.is_allowed_to_send(QUICFrameType::STREAM)) {
    return frame;
  }

  uint64_t maximum_data_size = 0;
  if (maximum_frame_size <= MAX_STREAM_FRAME_OVERHEAD) {
    return frame;
  }
  maximum_data_size = maximum_frame_size - MAX_STREAM_FRAME_OVERHEAD;

  bool pure_fin = false;
  bool fin      = false;
  if (this->_adapter->is_eos()) {
    // Pure FIN stream should be sent regardless status of remote flow controller, because the length is zero.
    pure_fin = true;
    fin      = true;
  }

  uint64_t len = 0;
  if (!pure_fin) {
    uint64_t data_len = this->_adapter->unread_len();
    if (data_len == 0) {
      return frame;
    }

    // Check Connection/Stream level credit only if the generating STREAM frame is not pure fin
    uint64_t stream_credit = this->_remote_flow_controller.credit();
    if (stream_credit == 0) {
      // STREAM_DATA_BLOCKED
      frame =
        this->_remote_flow_controller.generate_frame(buf, level, UINT16_MAX, maximum_frame_size, current_packet_size, seq_num);
      return frame;
    }

    if (connection_credit == 0) {
      // BLOCKED - BLOCKED frame will be sent by connection level remote flow controller
      return frame;
    }

    len = std::min(data_len, std::min(maximum_data_size, std::min(stream_credit, connection_credit)));

    // data_len, maximum_data_size, stream_credit and connection_credit are already checked they're larger than 0
    ink_assert(len != 0);

    if (this->_adapter->total_len() == this->_send_offset + len) {
      fin = true;
    }
  }

  Ptr<IOBufferBlock> block = this->_adapter->read(len);
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

    QUICVStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                       this->_remote_flow_controller.current_limit());
    if (this->_remote_flow_controller.current_offset() == this->_remote_flow_controller.current_limit()) {
      QUICStreamDebug("Flow Controller will block sending a STREAM frame");
    }

    this->_send_offset += len;
  }
  this->_records_stream_frame(level, *static_cast<QUICStreamFrame *>(frame));

  this->_adapter->encourge_write();
  if (this->_state.update_with_sending_frame(*frame)) {
    this->_notify_state_change();
  }

  return frame;
}

void
QUICBidirectionalStream::_on_frame_acked(QUICFrameInformationUPtr &info)
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

  if (this->_state.update_on_ack()) {
    this->_notify_state_change();
  }
}

void
QUICBidirectionalStream::_on_frame_lost(QUICFrameInformationUPtr &info)
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

void
QUICBidirectionalStream::stop_sending(QUICStreamErrorUPtr error)
{
  this->_stop_sending_reason = std::move(error);
}

void
QUICBidirectionalStream::reset(QUICStreamErrorUPtr error)
{
  this->_reset_reason = std::move(error);
}

void
QUICBidirectionalStream::on_read()
{
  if (this->_state.update_on_read()) {
    this->_notify_state_change();
  }
}

void
QUICBidirectionalStream::on_eos()
{
  if (this->_state.update_on_eos()) {
    this->_notify_state_change();
  }
}

QUICOffset
QUICBidirectionalStream::largest_offset_received() const
{
  return this->_local_flow_controller.current_offset();
}

QUICOffset
QUICBidirectionalStream::largest_offset_sent() const
{
  return this->_remote_flow_controller.current_offset();
}

void
QUICBidirectionalStream::_on_adapter_updated()
{
  this->_progress_sa.set_stream_adapter(this->_adapter);
}
