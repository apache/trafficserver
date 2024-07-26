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

#include "catch.hpp"

#include "proxy/http3/Http3FrameDispatcher.h"
#include "proxy/http3/Http3ProtocolEnforcer.h"
#include "Mock.h"

TEST_CASE("Http3FrameHandler dispatch", "[http3]")
{
  Http3FrameDispatcher  http3FrameDispatcher;
  Http3MockFrameHandler handler;
  Http3ProtocolEnforcer enforcer;
  http3FrameDispatcher.add_handler(&handler);
  http3FrameDispatcher.add_handler(&enforcer);

  MIOBuffer      *buf    = new_MIOBuffer(BUFFER_SIZE_INDEX_512);
  IOBufferReader *reader = buf->alloc_reader();
  uint64_t        nread  = 0;
  Http3ErrorUPtr  error  = Http3ErrorUPtr(nullptr);

  SECTION("Test good case")
  {
    uint8_t input[] = {// 1st frame (HEADERS)
                       0x01, 0x04, 0x11, 0x22, 0x33, 0x44,
                       // 2nd frame (DATA)
                       0x00, 0x04, 0xaa, 0xbb, 0xcc, 0xdd,
                       // 3rd frame (incomplete)
                       0xff};

    buf->write(input, sizeof(input));

    // Initial state
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 0);

    error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::UNKNOWN, *reader, nread);
    CHECK(!error);
    CHECK(handler.total_frame_received == 1);
    CHECK(nread == 12);
  }

  SECTION("Test good case with a multibyte frame type encoding")
  {
    uint8_t input[] = {// 1st frame (HEADERS)
                       0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44,
                       // 2nd frame (DATA)
                       0x00, 0x04, 0xaa, 0xbb, 0xcc, 0xdd,
                       // 3rd frame (incomplete)
                       0xff};

    // Initial state
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 0);

    SECTION("Write everything at once")
    {
      buf->write(input, sizeof(input));
      error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::UNKNOWN, *reader, nread);
      CHECK(!error);
      CHECK(handler.total_frame_received == 1);
      CHECK(nread == 19);
    }

    SECTION("Write one byte at a time")
    {
      int total_nread{};
      for (uint8_t *it{input}; it < input + sizeof(input); ++it) {
        buf->write(it, 1);
        error        = http3FrameDispatcher.on_read_ready(0, Http3StreamType::UNKNOWN, *reader, nread);
        total_nread += nread;
        CHECK(!error);
      }
      CHECK(handler.total_frame_received == 5);
      CHECK(total_nread == 19);
    }
  }

  free_MIOBuffer(buf);
}

TEST_CASE("control stream tests", "[http3]")
{
  Http3FrameDispatcher  http3FrameDispatcher;
  Http3ProtocolEnforcer enforcer;
  Http3MockFrameHandler handler;

  http3FrameDispatcher.add_handler(&enforcer);
  http3FrameDispatcher.add_handler(&handler);

  MIOBuffer      *buf    = new_MIOBuffer(BUFFER_SIZE_INDEX_512);
  IOBufferReader *reader = buf->alloc_reader();
  uint64_t        nread  = 0;
  Http3ErrorUPtr  error  = Http3ErrorUPtr(nullptr);

  SECTION("Only one SETTINGS frame is allowed per the control stream")
  {
    uint8_t input[] = {
      0x04,       // Type
      0x08,       // Length
      0x06,       // Identifier
      0x44, 0x00, // Value
      0x09,       // Identifier
      0x0f,       // Value
      0x4a, 0x0a, // Identifier
      0x00,       // Value
      0x04,       // Type
      0x08,       // Length
      0x06,       // Identifier
      0x44, 0x00, // Value
      0x09,       // Identifier
      0x0f,       // Value
      0x4a, 0x0a, // Identifier
      0x00,       // Value
    };

    buf->write(input, sizeof(input));

    // Initial state
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 0);

    error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::CONTROL, *reader, nread);
    REQUIRE(error);
    CHECK(error->code == Http3ErrorCode::H3_FRAME_UNEXPECTED);
    CHECK(handler.total_frame_received == 1);
    CHECK(nread == sizeof(input));
  }

  SECTION("first frame of the control stream must be SETTINGS frame")
  {
    uint8_t input[] = {
      0x0d,       // Type
      0x01,       // Length
      0x01,       // Push ID
      0x04,       // Type
      0x08,       // Length
      0x06,       // Identifier
      0x44, 0x00, // Value
      0x09,       // Identifier
      0x0f,       // Value
      0x4a, 0x0a, // Identifier
      0x00,       // Value
    };

    buf->write(input, sizeof(input));

    // Initial state
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 0);

    error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::CONTROL, *reader, nread);
    REQUIRE(error);
    CHECK(error->code == Http3ErrorCode::H3_MISSING_SETTINGS);
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 3);
  }

  SECTION("DATA frame is not allowed on control stream")
  {
    uint8_t input[] = {0x04,       // Type
                       0x08,       // Length
                       0x06,       // Identifier
                       0x44, 0x00, // Value
                       0x09,       // Identifier
                       0x0f,       // Value
                       0x4a, 0x0a, // Identifier
                       0x00,       // Value
                       0x00,       // Type
                       0x04,       // Length
                       0x11, 0x22, 0x33, 0x44};

    buf->write(input, sizeof(input));

    // Initial state
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 0);

    error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::CONTROL, *reader, nread);
    REQUIRE(error);
    CHECK(error->code == Http3ErrorCode::H3_FRAME_UNEXPECTED);
    CHECK(handler.total_frame_received == 1);
    CHECK(nread == sizeof(input));
  }

  SECTION("HEADERS frame is not allowed on control stream")
  {
    uint8_t input[] = {0x04,       // Type
                       0x08,       // Length
                       0x06,       // Identifier
                       0x44, 0x00, // Value
                       0x09,       // Identifier
                       0x0f,       // Value
                       0x4a, 0x0a, // Identifier
                       0x00,       // Value
                       0x01,       // Type
                       0x04,       // Length
                       0x11, 0x22, 0x33, 0x44};

    buf->write(input, sizeof(input));

    // Initial state
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 0);

    error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::CONTROL, *reader, nread);
    REQUIRE(error);
    CHECK(error->code == Http3ErrorCode::H3_FRAME_UNEXPECTED);
    CHECK(handler.total_frame_received == 1);
    CHECK(nread == sizeof(input));
  }

  SECTION("RESERVED frame is not allowed on control stream")
  {
    uint8_t input[] = {0x04,       // Type
                       0x08,       // Length
                       0x06,       // Identifier
                       0x44, 0x00, // Value
                       0x09,       // Identifier
                       0x0f,       // Value
                       0x4a, 0x0a, // Identifier
                       0x00,       // Value
                       0x06,       // Type
                       0x04,       // Length
                       0x11, 0x22, 0x33, 0x44};

    buf->write(input, sizeof(input));

    // Initial state
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 0);

    error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::CONTROL, *reader, nread);
    REQUIRE(error);
    CHECK(error->code == Http3ErrorCode::H3_FRAME_UNEXPECTED);
    CHECK(handler.total_frame_received == 1);
    CHECK(nread == sizeof(input));
  }

  free_MIOBuffer(buf);
}

// This test needs to run without an enforcer due to a frame counting bug.
// Add a ProtocolEnforcer handler to reproduce.
TEST_CASE("padding should not be interpreted as a DATA frame", "[http3]")
{
  Http3FrameDispatcher  http3FrameDispatcher;
  Http3MockFrameHandler handler;

  http3FrameDispatcher.add_handler(&handler);

  MIOBuffer      *buf    = new_MIOBuffer(BUFFER_SIZE_INDEX_512);
  IOBufferReader *reader = buf->alloc_reader();
  uint64_t        nread  = 0;
  Http3ErrorUPtr  error  = Http3ErrorUPtr(nullptr);

  uint8_t input[] = {
    0x40, 0x04, // Type
    0x03,       // Length
    0x06,       // Identifier
    0x44, 0x00, // Value
  };

  // Initial state
  CHECK(handler.total_frame_received == 0);
  CHECK(nread == 0);

  int total_nread{};
  for (uint8_t *it{input}; it < input + sizeof(input); ++it) {
    buf->write(it, 1);
    error        = http3FrameDispatcher.on_read_ready(0, Http3StreamType::CONTROL, *reader, nread);
    total_nread += nread;
    CHECK(!error);
  }

  CHECK(handler.total_frame_received == 1);
  CHECK(total_nread == 6);

  free_MIOBuffer(buf);
}

TEST_CASE("ignore unknown frames", "[http3]")
{
  SECTION("ignore unkown frame")
  {
    uint8_t input[] = {
      0x0f // Type
    };

    Http3FrameDispatcher http3FrameDispatcher;

    MIOBuffer      *buf    = new_MIOBuffer(BUFFER_SIZE_INDEX_512);
    IOBufferReader *reader = buf->alloc_reader();
    uint64_t        nread  = 0;
    Http3ErrorUPtr  error  = Http3ErrorUPtr(nullptr);

    buf->write(input, sizeof(input));

    CHECK(nread == 0);
    error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::UNKNOWN, *reader, nread);
    CHECK(!error);
    CHECK(nread == 0);
    free_MIOBuffer(buf);
  }
}

TEST_CASE("Reserved frame type not allowed", "[http3]")
{
  SECTION("Reject reserved frame type in non control stream")
  {
    uint8_t input[] = {// 1st frame (HEADERS)
                       0x01, 0x04, 0x11, 0x22, 0x33, 0x44,
                       // 2nd frame (DATA)
                       0x06, 0x04, 0xaa, 0xbb, 0xcc, 0xdd,
                       // 3rd frame (incomplete)
                       0xff};

    Http3FrameDispatcher  http3FrameDispatcher;
    Http3MockFrameHandler handler;
    Http3ProtocolEnforcer enforcer;
    http3FrameDispatcher.add_handler(&handler);
    http3FrameDispatcher.add_handler(&enforcer);

    MIOBuffer      *buf    = new_MIOBuffer(BUFFER_SIZE_INDEX_512);
    IOBufferReader *reader = buf->alloc_reader();
    uint64_t        nread  = 0;
    Http3ErrorUPtr  error  = Http3ErrorUPtr(nullptr);

    buf->write(input, sizeof(input));

    // Initial state
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 0);

    error = http3FrameDispatcher.on_read_ready(0, Http3StreamType::UNKNOWN, *reader, nread);
    REQUIRE(error);
    CHECK(error->code == Http3ErrorCode::H3_FRAME_UNEXPECTED);
    CHECK(handler.total_frame_received == 0);
    CHECK(nread == 12);
    free_MIOBuffer(buf);
  }
}
