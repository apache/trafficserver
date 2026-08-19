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

#include "iocore/net/quic/QUICStream.h"
#include "iocore/net/quic/QUICStreamAdapter.h"

#include <algorithm>

constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD = 24;

QUICStream::QUICStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid) : _connection_info(cinfo), _id(sid) {}

QUICStream::~QUICStream() {}

size_t
QUICStream::compute_fair_send_budget(size_t num_writable_streams)
{
  if (num_writable_streams <= 1) {
    return MAX_STREAM_SEND_BYTES_PER_EVENT;
  }
  return std::clamp(MAX_STREAM_SEND_BYTES_PER_EVENT / num_writable_streams, MIN_STREAM_SEND_BYTES_PER_EVENT,
                    MAX_STREAM_SEND_BYTES_PER_EVENT);
}

QUICStreamId
QUICStream::id() const
{
  return this->_id;
}

const QUICConnectionInfoProvider *
QUICStream::connection_info()
{
  return this->_connection_info;
}

QUICStreamDirection
QUICStream::direction() const
{
  return QUICTypeUtil::detect_stream_direction(this->_id, this->_connection_info->direction());
}

bool
QUICStream::is_bidirectional() const
{
  return ((this->_id & 0x03) < 0x02);
}

bool
QUICStream::has_no_more_data() const
{
  return this->_has_no_more_data;
}

bool
QUICStream::has_data_to_send()
{
  if (this->_pending_send_block) {
    return true;
  }
  if (this->_adapter == nullptr) {
    return false;
  }

  const bool has_buffered_data = this->_adapter->unread_len() > 0;
  const bool needs_fin         = !this->_sent_fin && this->_adapter->is_eos() && this->_adapter->total_len() == this->_sent_bytes;

  return has_buffered_data || needs_fin;
}

void
QUICStream::set_io_adapter(QUICStreamAdapter *adapter)
{
  this->_adapter = adapter;
}

QUICOffset
QUICStream::final_offset() const
{
  return 0;
}

void
QUICStream::stop_sending(QUICStreamErrorUPtr /* error ATS_UNUSED */)
{
}

void
QUICStream::reset(QUICStreamErrorUPtr /* error ATS_UNUSED */)
{
}

void
QUICStream::on_read()
{
}

void
QUICStream::on_write()
{
  if (this->_connection_info != nullptr) {
    this->_connection_info->on_stream_updated();
  }
}

void
QUICStream::on_eos()
{
}

void
QUICStream::receive_data(QUICStreamIO &stream_io)
{
  uint8_t                    buf[4096];
  bool                       fin       = false;
  bool                       delivered = false;
  ssize_t                    read_len  = 0;
  [[maybe_unused]] ErrorCode error_code{0};

  while ((read_len = stream_io.read_stream(this->_id, buf, sizeof(buf), fin, error_code)) > 0) {
    this->_adapter->write(this->_received_bytes, buf, read_len, fin);
    this->_received_bytes += read_len;
    delivered              = true;
  }

  if (read_len == 0 && fin && !this->_has_no_more_data) {
    this->_adapter->write(this->_received_bytes, buf, 0, true);
    delivered = true;
  }
  this->_has_no_more_data = stream_io.stream_read_finished(this->_id);

  if (delivered) {
    this->_adapter->encourge_read();
  }
}

int64_t
QUICStream::send_data(QUICStreamIO &stream_io, size_t max_bytes_this_event)
{
  bool                       fin = false;
  ssize_t                    len = 0;
  [[maybe_unused]] ErrorCode error_code{0};
  size_t                     written_this_event = 0;
  // _write_vio.nbytes is set once when the VIO is armed and doesn't change over the
  // course of this call, so query it once instead of re-locking the adapter's mutex
  // for the same value on every loop iteration below.
  const uint64_t total_len = this->_adapter->total_len();

  while (written_this_event < max_bytes_this_event) {
    len = stream_io.stream_write_capacity(this->_id);
    if (len <= 0) {
      return written_this_event;
    }

    if (!this->_pending_send_block) {
      size_t read_len           = std::min(static_cast<size_t>(len), max_bytes_this_event - written_this_event);
      this->_pending_send_block = this->_adapter->read(read_len);
      if (!this->_pending_send_block) {
        if (!this->_sent_fin && this->_adapter->is_eos() && total_len == this->_sent_bytes) {
          static constexpr uint8_t empty_data  = 0;
          ssize_t                  written_len = stream_io.write_stream(this->_id, &empty_data, 0, true, error_code);
          if (written_len >= 0) {
            this->_sent_fin = true;
            return written_this_event + static_cast<size_t>(written_len);
          }
        }
        this->_adapter->encourge_write();
        return written_this_event;
      }
      this->_pending_send_fin = total_len == this->_sent_bytes + this->_pending_send_block->size();
    }

    Ptr<IOBufferBlock> block = this->_pending_send_block;
    // A pending block may have been carried over from a previous event, sized under
    // that event's (possibly larger) budget. Cap what we submit here to what's left
    // of this event's budget, and only claim fin if the whole remaining block is
    // included in this submission.
    size_t remaining_budget = max_bytes_this_event - written_this_event;
    size_t to_write         = std::min(static_cast<size_t>(block->size()), remaining_budget);
    fin                     = this->_pending_send_fin && to_write == static_cast<size_t>(block->size());
    if (block->size() == 0 && !fin) {
      this->_pending_send_block = nullptr;
      this->_pending_send_fin   = false;
      this->_adapter->encourge_write();
      continue;
    }

    if (to_write > 0 || fin) {
      ssize_t written_len =
        stream_io.write_stream(this->_id, reinterpret_cast<uint8_t *>(block->start()), to_write, fin, error_code);
      if (written_len >= 0) {
        this->_adapter->consume(written_len);
        this->_sent_bytes  += written_len;
        written_this_event += static_cast<size_t>(written_len);
        block->consume(written_len);
        if (block->size() == 0) {
          this->_pending_send_block = nullptr;
          this->_pending_send_fin   = false;
          this->_sent_fin           = fin;
        } else {
          return written_this_event;
        }
        if (!this->has_data_to_send()) {
          this->_adapter->encourge_write();
          return written_this_event;
        }
        continue;
      }
    }
    this->_adapter->encourge_write();
    return written_this_event;
  }

  return written_this_event;
}
