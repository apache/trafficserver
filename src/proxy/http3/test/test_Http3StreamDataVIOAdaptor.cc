/** @file
 *
 *  Unit tests for Http3StreamDataVIOAdaptor body buffering.
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

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "iocore/eventsystem/VIO.h"
#include "iocore/eventsystem/IOBuffer.h"
#include "proxy/http3/Http3Frame.h"
#include "proxy/http3/Http3StreamDataVIOAdaptor.h"

namespace
{
// Build a single DATA frame (type 0x00 + 1-byte length + payload) in the
// caller-owned buffer and return a frame object that reads from it. The caller
// must keep @a buf alive for the returned frame's lifetime.
std::shared_ptr<Http3DataFrame>
make_data_frame(MIOBuffer *buf, uint8_t payload_len, uint8_t fill)
{
  IOBufferReader *reader   = buf->alloc_reader();
  uint8_t         header[] = {0x00, payload_len}; // payload_len < 64 -> 1-byte varint
  buf->write(header, sizeof(header));

  std::vector<uint8_t> payload(payload_len, fill);
  buf->write(payload.data(), payload.size());

  return std::make_shared<Http3DataFrame>(*reader);
}
} // namespace

TEST_CASE("Http3StreamDataVIOAdaptor delivers a multi-frame body intact", "[http3]")
{
  // Each DATA frame appends a separate block to the adaptor's internal
  // buffer, so several frames leave the buffer spanning multiple blocks.
  // Allocating the drain reader at finalize time would reset it to the
  // current write tail and deliver only the last frame's bytes; every frame
  // must reach the sink.
  constexpr int     frame_count     = 4;
  constexpr uint8_t per_frame_bytes = 50;
  constexpr int64_t expected        = static_cast<int64_t>(frame_count) * per_frame_bytes;

  // Sink VIO with a writer buffer; allocate its reader while empty so it is
  // anchored at the head and observes everything finalize() writes.
  MIOBuffer      *sink_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *sink_reader = sink_buffer->alloc_reader();
  VIO             sink_vio;
  sink_vio.mutex = new_ProxyMutex();
  sink_vio.set_writer(sink_buffer);

  std::vector<MIOBuffer *> frame_buffers;
  {
    Http3StreamDataVIOAdaptor adaptor(&sink_vio);

    for (int i = 0; i < frame_count; ++i) {
      MIOBuffer *fb = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
      frame_buffers.push_back(fb);
      auto frame = make_data_frame(fb, per_frame_bytes, static_cast<uint8_t>('A' + i));
      adaptor.handle_frame(frame);
    }

    REQUIRE(adaptor.has_data());

    adaptor.finalize();

    CHECK(sink_reader->read_avail() == expected);
    CHECK(sink_vio.nbytes == expected);
  }

  free_MIOBuffer(sink_buffer);
  for (auto *fb : frame_buffers) {
    free_MIOBuffer(fb);
  }
}
