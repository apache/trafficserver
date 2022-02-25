/** @file

    Unit tests for Http2Frame

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

#include "catch.hpp"

#include "Http2Frame.h"

TEST_CASE("Http2Frame", "[http2][Http2Frame]")
{
  MIOBuffer *miob        = new_MIOBuffer();
  IOBufferReader *miob_r = miob->alloc_reader();

  SECTION("PUSH_PROMISE")
  {
    Http2StreamId id = 1;
    uint8_t flags    = HTTP2_FLAGS_PUSH_PROMISE_END_HEADERS;
    Http2PushPromise pp{0, 2};
    uint8_t hdr_block[]   = {0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef};
    uint8_t hdr_block_len = sizeof(hdr_block);

    Http2PushPromiseFrame frame(id, flags, pp, hdr_block, hdr_block_len);
    int64_t written = frame.write_to(miob);

    CHECK(written == static_cast<int64_t>(HTTP2_FRAME_HEADER_LEN + sizeof(Http2StreamId) + hdr_block_len));
    CHECK(written == miob_r->read_avail());

    uint8_t buf[32] = {0};
    int64_t read    = miob_r->read(buf, written);
    CHECK(read == written);

    uint8_t expected[] = {
      0x00, 0x00, 0x0e,                                          ///< Length
      0x05,                                                      ///< Type
      0x04,                                                      ///< Flags
      0x00, 0x00, 0x00, 0x01,                                    ///< Stream Identifier (31)
      0x00, 0x00, 0x00, 0x02,                                    ///< Promised Stream ID
      0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef ///< Header Block Fragment
    };

    CHECK(memcmp(buf, expected, written) == 0);
  }

  free_MIOBuffer(miob);
}
