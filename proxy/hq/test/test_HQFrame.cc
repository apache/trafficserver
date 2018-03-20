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
#include "HQFrame.h"
#include "HQFrameDispatcher.h"

TEST_CASE("HQFrame Type", "[hq]")
{
  CHECK(HQFrame::type(reinterpret_cast<const uint8_t *>("\x00\x00"), 2) == HQFrameType::DATA);
  // Undefined ragne
  CHECK(HQFrame::type(reinterpret_cast<const uint8_t *>("\x00\x0e"), 2) == HQFrameType::UNKNOWN);
  CHECK(HQFrame::type(reinterpret_cast<const uint8_t *>("\x00\xff"), 2) == HQFrameType::UNKNOWN);
}

TEST_CASE("Load DATA Frame", "[hq]")
{
  SECTION("No flags")
  {
    uint8_t buf1[] = {
      0x04,                   // Length
      0x00,                   // Type
      0x00,                   // Flags
      0x11, 0x22, 0x33, 0x44, // Payload
    };
    std::shared_ptr<const HQFrame> frame1 = HQFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == HQFrameType::DATA);
    CHECK(frame1->length() == 4);
    std::shared_ptr<const HQDataFrame> data_frame = std::dynamic_pointer_cast<const HQDataFrame>(frame1);
    CHECK(data_frame);
    CHECK(data_frame->payload_length() == 4);
    CHECK(memcmp(data_frame->payload(), "\x11\x22\x33\x44", 4) == 0);
  }

  SECTION("Have flags (invalid)")
  {
    uint8_t buf1[] = {
      0x04,                   // Length
      0x00,                   // Type
      0xff,                   // Flags
      0x11, 0x22, 0x33, 0x44, // Payload
    };
    std::shared_ptr<const HQFrame> frame1 = HQFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == HQFrameType::DATA);
    CHECK(frame1->length() == 4);
    std::shared_ptr<const HQDataFrame> data_frame = std::dynamic_pointer_cast<const HQDataFrame>(frame1);
    CHECK(data_frame);
    CHECK(data_frame->payload_length() == 4);
    CHECK(memcmp(data_frame->payload(), "\x11\x22\x33\x44", 4) == 0);
  }
}

TEST_CASE("Store DATA Frame", "[hq]")
{
  SECTION("Normal")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected1[] = {
      0x04,                   // Length
      0x00,                   // Type
      0x00,                   // Flags
      0x11, 0x22, 0x33, 0x44, // Payload
    };

    uint8_t raw1[]          = "\x11\x22\x33\x44";
    ats_unique_buf payload1 = ats_unique_malloc(4);
    memcpy(payload1.get(), raw1, 4);

    HQDataFrame data_frame(std::move(payload1), 4);
    CHECK(data_frame.length() == 4);

    data_frame.store(buf, &len);
    CHECK(len == 7);
    CHECK(memcmp(buf, expected1, len) == 0);
  }
}

TEST_CASE("HQFrameFactory Create Unknown Frame", "[hq]")
{
  uint8_t buf1[] = {
    0x00, // Length
    0xff, // Type
    0x00, // Flags
  };
  std::shared_ptr<const HQFrame> frame1 = HQFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1);
  CHECK(frame1->type() == HQFrameType::UNKNOWN);
  CHECK(frame1->length() == 0);
}

TEST_CASE("HQFrameFactory Fast Create Frame", "[hq]")
{
  HQFrameFactory factory;

  uint8_t buf1[] = {
    0x04,                   // Length
    0x00,                   // Type
    0x00,                   // Flags
    0x11, 0x22, 0x33, 0x44, // Payload
  };
  uint8_t buf2[] = {
    0x04,                   // Length
    0x00,                   // Type
    0x00,                   // Flags
    0xaa, 0xbb, 0xcc, 0xdd, // Payload
  };
  std::shared_ptr<const HQFrame> frame1 = factory.fast_create(buf1, sizeof(buf1));
  CHECK(frame1 != nullptr);

  std::shared_ptr<const HQDataFrame> data_frame1 = std::dynamic_pointer_cast<const HQDataFrame>(frame1);
  CHECK(data_frame1 != nullptr);
  CHECK(memcmp(data_frame1->payload(), buf1 + 3, 4) == 0);

  std::shared_ptr<const HQFrame> frame2 = factory.fast_create(buf2, sizeof(buf2));
  CHECK(frame2 != nullptr);

  std::shared_ptr<const HQDataFrame> data_frame2 = std::dynamic_pointer_cast<const HQDataFrame>(frame2);
  CHECK(data_frame2 != nullptr);
  CHECK(memcmp(data_frame2->payload(), buf2 + 3, 4) == 0);

  CHECK(frame1 == frame2);
}

TEST_CASE("HQFrameFactory Fast Create Unknown Frame", "[hq]")
{
  HQFrameFactory factory;

  uint8_t buf1[] = {
    0x0f, // Type
  };
  std::shared_ptr<const HQFrame> frame1 = factory.fast_create(buf1, sizeof(buf1));
  CHECK(frame1 == nullptr);
}
