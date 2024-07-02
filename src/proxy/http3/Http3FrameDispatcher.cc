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

#include "proxy/http3/Http3FrameDispatcher.h"

#include "tscore/Diags.h"
#include "iocore/net/quic/QUICIntUtil.h"

#include "proxy/http3/Http3DebugNames.h"

namespace
{
DbgCtl dbg_ctl_v_http3{"v_http3"};
DbgCtl dbg_ctl_http3{"http3"};

} // end anonymous namespace

//
// Frame Dispatcher
//

void
Http3FrameDispatcher::add_handler(Http3FrameHandler *handler)
{
  for (Http3FrameType t : handler->interests()) {
    this->_handlers[static_cast<uint8_t>(t)].push_back(handler);
  }
}

Http3ErrorUPtr
Http3FrameDispatcher::on_read_ready(QUICStreamId stream_id, Http3StreamType stream_type, IOBufferReader &reader, uint64_t &nread)
{
  Http3ErrorUPtr error = Http3ErrorUPtr(nullptr);
  nread                = 0;
  uint32_t frame_count = 0;

  while (true) {
    // Read a length of Type field and hopefully a length of Length field too
    uint8_t head[16];
    auto    p        = reader.memcpy(head, 16);
    int64_t read_len = p - reinterpret_cast<char *>(head);
    Dbg(dbg_ctl_v_http3, "reading H3 frame: state=%d read_len=%" PRId64, this->_reading_state, read_len);

    if (this->_reading_state == READING_TYPE_LEN) {
      if (read_len >= 1) {
        this->_reading_frame_type_len = QUICVariableInt::size(head);
        this->_reading_state          = READING_LENGTH_LEN;
        Dbg(dbg_ctl_v_http3, "type_len=%" PRId64, this->_reading_frame_type_len);
      } else {
        break;
      }
    }

    if (this->_reading_state == READING_LENGTH_LEN) {
      if (read_len >= this->_reading_frame_type_len + 1) {
        this->_reading_frame_length_len = QUICVariableInt::size(head + this->_reading_frame_type_len);
        this->_reading_state            = READING_PAYLOAD_LEN;
        Dbg(dbg_ctl_v_http3, "length_len=%" PRId64, this->_reading_frame_length_len);
      } else {
        break;
      }
    }

    if (this->_reading_state == READING_PAYLOAD_LEN) {
      if (read_len >= this->_reading_frame_type_len + this->_reading_frame_length_len) {
        size_t dummy;
        QUICVariableInt::decode(this->_reading_frame_payload_len, dummy, head + this->_reading_frame_type_len);
        Dbg(dbg_ctl_v_http3, "payload_len=%" PRId64, this->_reading_frame_payload_len);
        this->_reading_state = READING_PAYLOAD;
      } else {
        break;
      }
    }

    if (this->_reading_state == READING_PAYLOAD) {
      if (read_len == 0) {
        break;
      }
      if (this->_current_frame == nullptr) {
        // Create a frame
        // Type field length + Length field length + Payload length
        size_t frame_len = this->_reading_frame_type_len + this->_reading_frame_length_len + this->_reading_frame_payload_len;

        // Create a reader to read one frame. The reader will be deallocated by the frame object created
        auto cloned_reader        = reader.clone();
        cloned_reader->size_limit = frame_len;
        this->_current_frame      = this->_frame_factory.fast_create(*cloned_reader);
        if (this->_current_frame == nullptr) {
          cloned_reader->dealloc();
          break;
        }
        this->_bytes_to_skip = this->_current_frame->total_length();
        ++frame_count;
      }

      auto skip = std::min(static_cast<uint64_t>(reader.read_avail()), this->_bytes_to_skip);
      reader.consume(skip);
      this->_bytes_to_skip -= skip;
      nread                += skip;

      if (this->_current_frame->update()) {
        // Dispatch
        Http3FrameType type = this->_current_frame->type();
        Debug("http3", "[RX] [%" PRIu64 "] | %s size=%" PRIu64 "/%" PRIu64, stream_id, Http3DebugNames::frame_type(type),
              this->_current_frame->total_length() - _bytes_to_skip, this->_current_frame->total_length());
        std::vector<Http3FrameHandler *> handlers = this->_handlers[static_cast<uint8_t>(type)];
        for (auto h : handlers) {
          error = h->handle_frame(this->_current_frame, frame_count - 1, stream_type);
          if (error && error->cls != Http3ErrorClass::UNDEFINED) {
            return error;
          }
        }
      }

      if (this->_bytes_to_skip == 0) {
        this->_current_frame = nullptr;
        this->_reading_state = READING_TYPE_LEN;
      }
    }
  }

  return error;
}
