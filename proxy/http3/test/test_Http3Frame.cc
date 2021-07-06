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
#include <cstdio>
#include "Http3Frame.h"
#include "Http3FrameDispatcher.h"

TEST_CASE("Http3Frame Type", "[http3]")
{
  CHECK(Http3Frame::type(reinterpret_cast<const uint8_t *>("\x00\x00"), 2) == Http3FrameType::DATA);
  // Undefined range
  CHECK(Http3Frame::type(reinterpret_cast<const uint8_t *>("\x0f\x00"), 2) == Http3FrameType::UNKNOWN);
  CHECK(Http3Frame::type(reinterpret_cast<const uint8_t *>("\xff\xff\xff\xff\xff\xff\xff\x00"), 9) == Http3FrameType::UNKNOWN);
}

TEST_CASE("Load DATA Frame", "[http3]")
{
  SECTION("No flags")
  {
    uint8_t buf1[] = {
      0x00,                   // Type
      0x04,                   // Length
      0x11, 0x22, 0x33, 0x44, // Payload
    };
    std::shared_ptr<const Http3Frame> frame1 = Http3FrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == Http3FrameType::DATA);
    CHECK(frame1->length() == 4);

    std::shared_ptr<const Http3DataFrame> data_frame = std::dynamic_pointer_cast<const Http3DataFrame>(frame1);
    CHECK(data_frame);
    CHECK(data_frame->payload_length() == 4);
    CHECK(memcmp(data_frame->payload(), "\x11\x22\x33\x44", 4) == 0);
  }

  SECTION("Have flags (invalid)")
  {
    uint8_t buf1[] = {
      0x00,                   // Type
      0x04,                   // Length
      0x11, 0x22, 0x33, 0x44, // Payload
    };
    std::shared_ptr<const Http3Frame> frame1 = Http3FrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == Http3FrameType::DATA);
    CHECK(frame1->length() == 4);

    std::shared_ptr<const Http3DataFrame> data_frame = std::dynamic_pointer_cast<const Http3DataFrame>(frame1);
    CHECK(data_frame);
    CHECK(data_frame->payload_length() == 4);
    CHECK(memcmp(data_frame->payload(), "\x11\x22\x33\x44", 4) == 0);
  }
}

TEST_CASE("Store DATA Frame", "[http3]")
{
  SECTION("Normal")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected1[] = {
      0x00,                   // Type
      0x04,                   // Length
      0x11, 0x22, 0x33, 0x44, // Payload
    };

    uint8_t raw1[]          = "\x11\x22\x33\x44";
    ats_unique_buf payload1 = ats_unique_malloc(4);
    memcpy(payload1.get(), raw1, 4);

    Http3DataFrame data_frame(std::move(payload1), 4);
    CHECK(data_frame.length() == 4);

    data_frame.store(buf, &len);
    CHECK(len == 6);
    CHECK(memcmp(buf, expected1, len) == 0);
  }
}

TEST_CASE("Store HEADERS Frame", "[http3]")
{
  SECTION("Normal")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected1[] = {
      0x01,                   // Type
      0x04,                   // Length
      0x11, 0x22, 0x33, 0x44, // Payload
    };

    uint8_t raw1[]              = "\x11\x22\x33\x44";
    ats_unique_buf header_block = ats_unique_malloc(4);
    memcpy(header_block.get(), raw1, 4);

    Http3HeadersFrame hdrs_frame(std::move(header_block), 4);
    CHECK(hdrs_frame.length() == 4);

    hdrs_frame.store(buf, &len);
    CHECK(len == 6);
    CHECK(memcmp(buf, expected1, len) == 0);
  }
}

TEST_CASE("Load SETTINGS Frame", "[http3]")
{
  SECTION("Normal")
  {
    uint8_t buf[] = {
      0x04,       // Type
      0x08,       // Length
      0x06,       // Identifier
      0x44, 0x00, // Value
      0x09,       // Identifier
      0x0f,       // Value
      0x4a, 0xba, // Identifier
      0x00,       // Value
    };

    std::shared_ptr<const Http3Frame> frame = Http3FrameFactory::create(buf, sizeof(buf));
    CHECK(frame->type() == Http3FrameType::SETTINGS);
    CHECK(frame->length() == sizeof(buf) - 2);

    std::shared_ptr<const Http3SettingsFrame> settings_frame = std::dynamic_pointer_cast<const Http3SettingsFrame>(frame);
    CHECK(settings_frame);
    CHECK(settings_frame->is_valid());
    CHECK(settings_frame->get(Http3SettingsId::MAX_HEADER_LIST_SIZE) == 0x0400);
    CHECK(settings_frame->get(Http3SettingsId::NUM_PLACEHOLDERS) == 0x0f);
  }
}

TEST_CASE("Store SETTINGS Frame", "[http3]")
{
  SECTION("Normal")
  {
    uint8_t expected[] = {
      0x04,       // Type
      0x08,       // Length
      0x06,       // Identifier
      0x44, 0x00, // Value
      0x09,       // Identifier
      0x0f,       // Value
      0x4a, 0x0a, // Identifier
      0x00,       // Value
    };

    Http3SettingsFrame settings_frame;
    settings_frame.set(Http3SettingsId::MAX_HEADER_LIST_SIZE, 0x0400);
    settings_frame.set(Http3SettingsId::NUM_PLACEHOLDERS, 0x0f);

    uint8_t buf[32] = {0};
    size_t len;
    settings_frame.store(buf, &len);
    CHECK(len == sizeof(expected));
    CHECK(memcmp(buf, expected, len) == 0);
  }

  SECTION("Normal from Client")
  {
    uint8_t expected[] = {
      0x04,       // Type
      0x06,       // Length
      0x06,       // Identifier
      0x44, 0x00, // Value
      0x4a, 0x0a, // Identifier
      0x00,       // Value
    };

    Http3SettingsFrame settings_frame;
    settings_frame.set(Http3SettingsId::MAX_HEADER_LIST_SIZE, 0x0400);

    uint8_t buf[32] = {0};
    size_t len;
    settings_frame.store(buf, &len);
    CHECK(len == sizeof(expected));
    CHECK(memcmp(buf, expected, len) == 0);
  }
}

TEST_CASE("Http3FrameFactory Create Unknown Frame", "[http3]")
{
  uint8_t buf1[] = {
    0x0f, // Type
    0x00, // Length
  };
  std::shared_ptr<const Http3Frame> frame1 = Http3FrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1);
  CHECK(frame1->type() == Http3FrameType::UNKNOWN);
  CHECK(frame1->length() == 0);
}

TEST_CASE("Http3FrameFactory Fast Create Frame", "[http3]")
{
  Http3FrameFactory factory;

  uint8_t buf1[] = {
    0x00,                   // Type
    0x04,                   // Length
    0x11, 0x22, 0x33, 0x44, // Payload
  };
  uint8_t buf2[] = {
    0x00,                   // Type
    0x04,                   // Length
    0xaa, 0xbb, 0xcc, 0xdd, // Payload
  };
  std::shared_ptr<const Http3Frame> frame1 = factory.fast_create(buf1, sizeof(buf1));
  CHECK(frame1 != nullptr);

  std::shared_ptr<const Http3DataFrame> data_frame1 = std::dynamic_pointer_cast<const Http3DataFrame>(frame1);
  CHECK(data_frame1 != nullptr);
  CHECK(memcmp(data_frame1->payload(), buf1 + 2, 4) == 0);

  std::shared_ptr<const Http3Frame> frame2 = factory.fast_create(buf2, sizeof(buf2));
  CHECK(frame2 != nullptr);

  std::shared_ptr<const Http3DataFrame> data_frame2 = std::dynamic_pointer_cast<const Http3DataFrame>(frame2);
  CHECK(data_frame2 != nullptr);
  CHECK(memcmp(data_frame2->payload(), buf2 + 2, 4) == 0);

  CHECK(frame1 == frame2);
}

TEST_CASE("Http3FrameFactory Fast Create Unknown Frame", "[http3]")
{
  Http3FrameFactory factory;

  uint8_t buf1[] = {
    0x0f, // Type
  };
  std::shared_ptr<const Http3Frame> frame1 = factory.fast_create(buf1, sizeof(buf1));
  CHECK(frame1);
  CHECK(frame1->type() == Http3FrameType::UNKNOWN);
}
