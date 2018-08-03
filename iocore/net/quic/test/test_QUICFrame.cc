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

#include "quic/Mock.h"
#include "quic/QUICFrame.h"
#include "quic/QUICStream.h"

TEST_CASE("QUICFrame Type", "[quic]")
{
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x00")) == QUICFrameType::PADDING);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x01")) == QUICFrameType::RST_STREAM);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x02")) == QUICFrameType::CONNECTION_CLOSE);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x03")) == QUICFrameType::APPLICATION_CLOSE);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x04")) == QUICFrameType::MAX_DATA);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x05")) == QUICFrameType::MAX_STREAM_DATA);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x06")) == QUICFrameType::MAX_STREAM_ID);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x07")) == QUICFrameType::PING);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x08")) == QUICFrameType::BLOCKED);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x09")) == QUICFrameType::STREAM_BLOCKED);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0a")) == QUICFrameType::STREAM_ID_BLOCKED);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0b")) == QUICFrameType::NEW_CONNECTION_ID);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0c")) == QUICFrameType::STOP_SENDING);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0d")) == QUICFrameType::ACK);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0e")) == QUICFrameType::PATH_CHALLENGE);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0f")) == QUICFrameType::PATH_RESPONSE);
  // Range of STREAM
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x10")) == QUICFrameType::STREAM);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x17")) == QUICFrameType::STREAM);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x18")) == QUICFrameType::CRYPTO);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x19")) == QUICFrameType::NEW_TOKEN);
  // Undefined ragne
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x21")) == QUICFrameType::UNKNOWN);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\xff")) == QUICFrameType::UNKNOWN);
}

TEST_CASE("Load STREAM Frame", "[quic]")
{
  SECTION("OLF=000")
  {
    uint8_t buf1[] = {
      0x10,                   // 0b00010OLF (OLF=000)
      0x01,                   // Stream ID
      0x01, 0x02, 0x03, 0x04, // Stream Data
    };
    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::STREAM);
    CHECK(frame1->size() == 6);
    std::shared_ptr<const QUICStreamFrame> stream_frame = std::dynamic_pointer_cast<const QUICStreamFrame>(frame1);
    CHECK(stream_frame->stream_id() == 0x01);
    CHECK(stream_frame->offset() == 0x00);
    CHECK(stream_frame->data_length() == 4);
    CHECK(memcmp(stream_frame->data(), "\x01\x02\x03\x04", 4) == 0);
    CHECK(stream_frame->has_fin_flag() == false);
  }

  SECTION("OLF=010")
  {
    uint8_t buf1[] = {
      0x12,                         // 0b00010OLF (OLF=010)
      0x01,                         // Stream ID
      0x05,                         // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Stream Data
    };
    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::STREAM);
    CHECK(frame1->size() == 8);
    std::shared_ptr<const QUICStreamFrame> stream_frame = std::dynamic_pointer_cast<const QUICStreamFrame>(frame1);
    CHECK(stream_frame->stream_id() == 0x01);
    CHECK(stream_frame->offset() == 0x00);
    CHECK(stream_frame->data_length() == 5);
    CHECK(memcmp(stream_frame->data(), "\x01\x02\x03\x04\x05", 5) == 0);
    CHECK(stream_frame->has_fin_flag() == false);
  }

  SECTION("OLF=110")
  {
    uint8_t buf1[] = {
      0x16,                         // 0b00010OLF (OLF=110)
      0x01,                         // Stream ID
      0x02,                         // Data Length
      0x05,                         // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Stream Data
    };
    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::STREAM);
    CHECK(frame1->size() == 9);

    std::shared_ptr<const QUICStreamFrame> stream_frame = std::dynamic_pointer_cast<const QUICStreamFrame>(frame1);
    CHECK(stream_frame->stream_id() == 0x01);
    CHECK(stream_frame->offset() == 0x02);
    CHECK(stream_frame->data_length() == 5);
    CHECK(memcmp(stream_frame->data(), "\x01\x02\x03\x04\x05", 5) == 0);
    CHECK(stream_frame->has_fin_flag() == false);
  }

  SECTION("OLF=111")
  {
    uint8_t buf1[] = {
      0x17,                         // 0b00010OLF (OLF=110)
      0x01,                         // Stream ID
      0x02,                         // Data Length
      0x05,                         // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Stream Data
    };
    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::STREAM);
    CHECK(frame1->size() == 9);

    std::shared_ptr<const QUICStreamFrame> stream_frame = std::dynamic_pointer_cast<const QUICStreamFrame>(frame1);
    CHECK(stream_frame->stream_id() == 0x01);
    CHECK(stream_frame->offset() == 0x02);
    CHECK(stream_frame->data_length() == 5);
    CHECK(memcmp(stream_frame->data(), "\x01\x02\x03\x04\x05", 5) == 0);
    CHECK(stream_frame->has_fin_flag() == true);
  }
}

TEST_CASE("Store STREAM Frame", "[quic]")
{
  SECTION("8bit stream id, 0bit offset")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected1[] = {
      0x12,                         // 0b00010OLF (OLF=010)
      0x01,                         // Stream ID
      0x05,                         // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Stream Data
    };

    uint8_t raw1[]          = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload1 = ats_unique_malloc(5);
    memcpy(payload1.get(), raw1, 5);

    QUICStreamFrame stream_frame(std::move(payload1), 5, 0x01, 0x00);
    CHECK(stream_frame.size() == 8);

    stream_frame.store(buf, &len, 32);
    CHECK(len == 8);
    CHECK(memcmp(buf, expected1, len) == 0);
  }

  SECTION("8bit stream id, 16bit offset")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected2[] = {
      0x16,                         // 0b00010OLF (OLF=110)
      0x01,                         // Stream ID
      0x01,                         // Offset
      0x05,                         // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Stream Data
    };
    uint8_t raw2[]          = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload2 = ats_unique_malloc(5);
    memcpy(payload2.get(), raw2, 5);

    QUICStreamFrame stream_frame(std::move(payload2), 5, 0x01, 0x01);
    CHECK(stream_frame.size() == 9);

    stream_frame.store(buf, &len, 32);
    CHECK(len == 9);
    CHECK(memcmp(buf, expected2, len) == 0);
  }

  SECTION("8bit stream id, 32bit offset")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected3[] = {
      0x16,                         // 0b00010OLF (OLF=110)
      0x01,                         // Stream ID
      0x80, 0x01, 0x00, 0x00,       // Offset
      0x05,                         // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Stream Data
    };
    uint8_t raw3[]          = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload3 = ats_unique_malloc(5);
    memcpy(payload3.get(), raw3, 5);

    QUICStreamFrame stream_frame(std::move(payload3), 5, 0x01, 0x010000);
    CHECK(stream_frame.size() == 12);

    stream_frame.store(buf, &len, 32);
    CHECK(len == 12);
    CHECK(memcmp(buf, expected3, len) == 0);
  }

  SECTION("8bit stream id, 64bit offset")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected4[] = {
      0x16,                                           // 0b00010OLF (OLF=110)
      0x01,                                           // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Offset
      0x05,                                           // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05,                   // Stream Data
    };
    uint8_t raw4[]          = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload4 = ats_unique_malloc(5);
    memcpy(payload4.get(), raw4, 5);

    QUICStreamFrame stream_frame(std::move(payload4), 5, 0x01, 0x0100000000);
    CHECK(stream_frame.size() == 16);

    stream_frame.store(buf, &len, 32);
    CHECK(len == 16);
    CHECK(memcmp(buf, expected4, len) == 0);
  }

  SECTION("16bit stream id, 64bit offset")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected5[] = {
      0x16,                                           // 0b00010OLF (OLF=110)
      0x41, 0x00,                                     // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Offset
      0x05,                                           // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05,                   // Stream Data
    };
    uint8_t raw5[]          = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload5 = ats_unique_malloc(5);
    memcpy(payload5.get(), raw5, 5);

    QUICStreamFrame stream_frame(std::move(payload5), 5, 0x0100, 0x0100000000);
    CHECK(stream_frame.size() == 17);

    stream_frame.store(buf, &len, 32);
    CHECK(len == 17);
    CHECK(memcmp(buf, expected5, len) == 0);
  }

  SECTION("24bit stream id, 64bit offset")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected6[] = {
      0x16,                                           // 0b00010OLF (OLF=110)
      0x80, 0x01, 0x00, 0x00,                         // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Offset
      0x05,                                           // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05,                   // Stream Data
    };
    uint8_t raw6[]          = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload6 = ats_unique_malloc(5);
    memcpy(payload6.get(), raw6, 5);

    QUICStreamFrame stream_frame(std::move(payload6), 5, 0x010000, 0x0100000000);
    CHECK(stream_frame.size() == 19);

    stream_frame.store(buf, &len, 32);
    CHECK(len == 19);
    CHECK(memcmp(buf, expected6, len) == 0);
  }

  SECTION("32bit stream id, 64bit offset")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected7[] = {
      0x16,                                           // 0b00010OLF (OLF=110)
      0x81, 0x00, 0x00, 0x00,                         // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Offset
      0x05,                                           // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05,                   // Stream Data
    };
    uint8_t raw7[]          = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload7 = ats_unique_malloc(5);
    memcpy(payload7.get(), raw7, 5);

    QUICStreamFrame stream_frame(std::move(payload7), 5, 0x01000000, 0x0100000000);
    CHECK(stream_frame.size() == 19);

    stream_frame.store(buf, &len, 32);
    CHECK(len == 19);
    CHECK(memcmp(buf, expected7, len) == 0);
  }

  SECTION("32bit stream id, 64bit offset, FIN bit")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected[] = {
      0x17,                                           // 0b00010OLF (OLF=111)
      0x81, 0x00, 0x00, 0x00,                         // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Offset
      0x05,                                           // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05,                   // Stream Data
    };
    uint8_t raw[]          = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload = ats_unique_malloc(5);
    memcpy(payload.get(), raw, 5);

    QUICStreamFrame stream_frame(std::move(payload), 5, 0x01000000, 0x0100000000, true);
    CHECK(stream_frame.size() == 19);

    stream_frame.store(buf, &len, 32);
    CHECK(len == 19);
    CHECK(memcmp(buf, expected, len) == 0);
  }

  SECTION("split stream frame")
  {
    size_t len;
    uint8_t buf1[] = {
      0x16,                                           // 0b00010OLF (OLF=110)
      0x01,                                           // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Offset
      0x0a,                                           // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05,                   // Stream Data
      0x11, 0x22, 0x33, 0x44, 0x55,
    };

    QUICFrameUPtr frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::STREAM);
    CHECK(frame1->size() == 21);
    QUICStreamFrame *stream_frame = dynamic_cast<QUICStreamFrame *>(frame1.get());
    CHECK(stream_frame->offset() == 0x100000000);
    CHECK(stream_frame->stream_id() == 0x01);
    CHECK(stream_frame->data_length() == 10);
    QUICStreamFrame *stream_frame2 = dynamic_cast<QUICStreamFrame *>(stream_frame->split(16));
    CHECK(stream_frame->stream_id() == 0x01);
    CHECK(stream_frame->data_length() == 5);
    CHECK(memcmp(stream_frame->data(), "\x01\x02\x03\x04\x05", stream_frame->data_length()) == 0);
    CHECK(stream_frame->offset() == 0x100000000);

    CHECK(stream_frame2->data_length() == 5);
    CHECK(memcmp(stream_frame2->data(), "\x11\x22\x33\x44\x55", stream_frame2->data_length()) == 0);
    CHECK(stream_frame2->offset() == 0x100000000 + 5);
    CHECK(stream_frame2->stream_id() == 0x01);
  }
}

TEST_CASE("CRYPTO Frame", "[quic]")
{
  SECTION("Loading")
  {
    uint8_t buf[] = {
      0x18,                         // Type
      0x80, 0x01, 0x00, 0x00,       // Offset
      0x05,                         // Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Crypto Data
    };
    std::shared_ptr<const QUICFrame> frame = QUICFrameFactory::create(buf, sizeof(buf));
    CHECK(frame->type() == QUICFrameType::CRYPTO);
    CHECK(frame->size() == sizeof(buf));

    std::shared_ptr<const QUICCryptoFrame> crypto_frame = std::dynamic_pointer_cast<const QUICCryptoFrame>(frame);
    CHECK(crypto_frame->offset() == 0x010000);
    CHECK(crypto_frame->data_length() == 5);
    CHECK(memcmp(crypto_frame->data(), "\x01\x02\x03\x04\x05", 5) == 0);
  }

  SECTION("Storing")
  {
    uint8_t buf[32] = {0};
    size_t len;
    uint8_t expected[] = {
      0x18,                         // Typr
      0x80, 0x01, 0x00, 0x00,       // Offset
      0x05,                         // Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Crypto Data
    };
    uint8_t raw_data[]     = "\x01\x02\x03\x04\x05";
    ats_unique_buf payload = ats_unique_malloc(5);
    memcpy(payload.get(), raw_data, 5);

    QUICCryptoFrame crypto_frame(std::move(payload), 5, 0x010000);
    CHECK(crypto_frame.size() == sizeof(expected));

    crypto_frame.store(buf, &len, 32);
    CHECK(len == sizeof(expected));
    CHECK(memcmp(buf, expected, sizeof(expected)) == 0);
  }
}

TEST_CASE("Load Ack Frame 1", "[quic]")
{
  SECTION("0 Ack Block, 8 bit packet number length, 8 bit block length")
  {
    uint8_t buf1[] = {
      0x0d,       // Type
      0x12,       // Largest Acknowledged
      0x74, 0x56, // Ack Delay
      0x00,       // Ack Block Count
      0x00,       // Ack Block Section
    };
    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::ACK);
    CHECK(frame1->size() == 6);
    std::shared_ptr<const QUICAckFrame> ack_frame1 = std::dynamic_pointer_cast<const QUICAckFrame>(frame1);
    CHECK(ack_frame1 != nullptr);
    CHECK(ack_frame1->ack_block_count() == 0);
    CHECK(ack_frame1->largest_acknowledged() == 0x12);
    CHECK(ack_frame1->ack_delay() == 0x3456);
  }

  SECTION("0 Ack Block, 8 bit packet number length, 8 bit block length")
  {
    uint8_t buf1[] = {
      0x0d,                   // Type
      0x80, 0x00, 0x00, 0x01, // Largest Acknowledged
      0x41, 0x71,             // Ack Delay
      0x00,                   // Ack Block Count
      0x80, 0x00, 0x00, 0x01, // Ack Block Section (First ACK Block Length)
    };
    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::ACK);
    CHECK(frame1->size() == 12);

    std::shared_ptr<const QUICAckFrame> ack_frame1 = std::dynamic_pointer_cast<const QUICAckFrame>(frame1);
    CHECK(ack_frame1 != nullptr);
    CHECK(ack_frame1->largest_acknowledged() == 0x01);
    CHECK(ack_frame1->ack_delay() == 0x0171);
    CHECK(ack_frame1->ack_block_count() == 0);

    const QUICAckFrame::AckBlockSection *section = ack_frame1->ack_block_section();
    CHECK(section->first_ack_block() == 0x01);
  }

  SECTION("2 Ack Block, 8 bit packet number length, 8 bit block length")
  {
    uint8_t buf1[] = {
      0x0d,                                           // Type
      0x12,                                           // Largest Acknowledged
      0x74, 0x56,                                     // Ack Delay
      0x02,                                           // Ack Block Count
      0x01,                                           // Ack Block Section (First ACK Block Length)
      0x02,                                           // Ack Block Section (Gap 1)
      0x43, 0x04,                                     // Ack Block Section (ACK Block 1 Length)
      0x85, 0x06, 0x07, 0x08,                         // Ack Block Section (Gap 2)
      0xc9, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, // Ack Block Section (ACK Block 2 Length)
    };

    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::ACK);
    CHECK(frame1->size() == 21);
    std::shared_ptr<const QUICAckFrame> ack_frame1 = std::dynamic_pointer_cast<const QUICAckFrame>(frame1);
    CHECK(ack_frame1 != nullptr);
    CHECK(ack_frame1->largest_acknowledged() == 0x12);
    CHECK(ack_frame1->ack_delay() == 0x3456);
    CHECK(ack_frame1->ack_block_count() == 2);

    const QUICAckFrame::AckBlockSection *section = ack_frame1->ack_block_section();
    CHECK(section->first_ack_block() == 0x01);
    auto ite = section->begin();
    CHECK(ite != section->end());
    CHECK(ite->gap() == 0x02);
    CHECK(ite->length() == 0x0304);
    ++ite;
    CHECK(ite != section->end());
    CHECK(ite->gap() == 0x05060708);
    CHECK(ite->length() == 0x090a0b0c0d0e0f10);
    ++ite;
    CHECK(ite == section->end());
  }
}

TEST_CASE("Store Ack Frame", "[quic]")
{
  SECTION("0 Ack Block, 8 bit packet number length, 8 bit block length")
  {
    uint8_t buf[32] = {0};
    size_t len;

    uint8_t expected[] = {
      0x0d,       // Type
      0x12,       // Largest Acknowledged
      0x74, 0x56, // Ack Delay
      0x00,       // Ack Block Count
      0x00,       // Ack Block Section
    };

    QUICAckFrame ack_frame(0x12, 0x3456, 0);
    CHECK(ack_frame.size() == 6);

    ack_frame.store(buf, &len, 32);
    CHECK(len == 6);
    CHECK(memcmp(buf, expected, len) == 0);
  }

  SECTION("2 Ack Block, 8 bit packet number length, 8 bit block length")
  {
    uint8_t buf[32] = {0};
    size_t len;

    uint8_t expected[] = {
      0x0d,                                           // Type
      0x12,                                           // Largest Acknowledged
      0x74, 0x56,                                     // Ack Delay
      0x02,                                           // Ack Block Count
      0x01,                                           // Ack Block Section (First ACK Block Length)
      0x02,                                           // Ack Block Section (Gap 1)
      0x43, 0x04,                                     // Ack Block Section (ACK Block 1 Length)
      0x85, 0x06, 0x07, 0x08,                         // Ack Block Section (Gap 2)
      0xc9, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, // Ack Block Section (ACK Block 2 Length)
    };
    QUICAckFrame ack_frame(0x12, 0x3456, 0x01);
    QUICAckFrame::AckBlockSection *section = ack_frame.ack_block_section();
    section->add_ack_block({0x02, 0x0304});
    section->add_ack_block({0x05060708, 0x090a0b0c0d0e0f10});
    CHECK(ack_frame.size() == 21);

    ack_frame.store(buf, &len, 32);
    CHECK(len == 21);
    CHECK(memcmp(buf, expected, len) == 0);
  }
}

TEST_CASE("Load RST_STREAM Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x01,                                          // Type
    0x92, 0x34, 0x56, 0x78,                        // Stream ID
    0x00, 0x01,                                    // Error Code
    0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Final Offset
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::RST_STREAM);
  CHECK(frame1->size() == 15);
  std::shared_ptr<const QUICRstStreamFrame> rst_stream_frame1 = std::dynamic_pointer_cast<const QUICRstStreamFrame>(frame1);
  CHECK(rst_stream_frame1 != nullptr);
  CHECK(rst_stream_frame1->error_code() == 0x0001);
  CHECK(rst_stream_frame1->stream_id() == 0x12345678);
  CHECK(rst_stream_frame1->final_offset() == 0x1122334455667788);
}

TEST_CASE("Store RST_STREAM Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x01,                                          // Type
    0x92, 0x34, 0x56, 0x78,                        // Stream ID
    0x00, 0x01,                                    // Error Code
    0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Final Offset
  };
  QUICRstStreamFrame rst_stream_frame(0x12345678, 0x0001, 0x1122334455667788);
  CHECK(rst_stream_frame.size() == 15);

  rst_stream_frame.store(buf, &len, 65535);
  CHECK(len == 15);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load Ping Frame", "[quic]")
{
  uint8_t buf[] = {
    0x07, // Type
  };
  std::shared_ptr<const QUICFrame> frame = QUICFrameFactory::create(buf, sizeof(buf));
  CHECK(frame->type() == QUICFrameType::PING);
  CHECK(frame->size() == 1);

  std::shared_ptr<const QUICPingFrame> ping_frame = std::dynamic_pointer_cast<const QUICPingFrame>(frame);
  CHECK(ping_frame != nullptr);
}

TEST_CASE("Store Ping Frame", "[quic]")
{
  uint8_t buf[16];
  size_t len;

  uint8_t expected[] = {
    0x07, // Type
  };

  QUICPingFrame frame(true);
  CHECK(frame.size() == 1);

  frame.store(buf, &len, 16);
  CHECK(len == 1);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load Padding Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x00, // Type
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::PADDING);
  CHECK(frame1->size() == 1);
  std::shared_ptr<const QUICPaddingFrame> paddingFrame1 = std::dynamic_pointer_cast<const QUICPaddingFrame>(frame1);
  CHECK(paddingFrame1 != nullptr);
}

TEST_CASE("Store Padding Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x00, // Type
  };
  QUICPaddingFrame padding_frame;
  padding_frame.store(buf, &len, 65535);
  CHECK(len == 1);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("ConnectionClose Frame", "[quic]")
{
  uint8_t reason_phrase[]  = {0x41, 0x42, 0x43, 0x44, 0x45};
  size_t reason_phrase_len = sizeof(reason_phrase);

  SECTION("loading w/ reason phrase")
  {
    uint8_t buf[] = {
      0x02,                        // Type
      0x00, 0x0A,                  // Error Code
      0x00,                        // Frame Type
      0x05,                        // Reason Phrase Length
      0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
    };

    std::shared_ptr<const QUICFrame> frame = QUICFrameFactory::create(buf, sizeof(buf));
    CHECK(frame->type() == QUICFrameType::CONNECTION_CLOSE);
    CHECK(frame->size() == sizeof(buf));

    std::shared_ptr<const QUICConnectionCloseFrame> conn_close_frame =
      std::dynamic_pointer_cast<const QUICConnectionCloseFrame>(frame);
    CHECK(conn_close_frame != nullptr);
    CHECK(conn_close_frame->error_code() == QUICTransErrorCode::PROTOCOL_VIOLATION);
    CHECK(conn_close_frame->frame_type() == QUICFrameType::UNKNOWN);
    CHECK(conn_close_frame->reason_phrase_length() == reason_phrase_len);
    CHECK(memcmp(conn_close_frame->reason_phrase(), reason_phrase, reason_phrase_len) == 0);
  }

  SECTION("loading w/o reason phrase")
  {
    uint8_t buf[] = {
      0x02,       // Type
      0x00, 0x0A, // Error Code
      0x01,       // Frame Type
      0x00,       // Reason Phrase Length
    };
    std::shared_ptr<const QUICFrame> frame = QUICFrameFactory::create(buf, sizeof(buf));
    CHECK(frame->type() == QUICFrameType::CONNECTION_CLOSE);
    CHECK(frame->size() == sizeof(buf));

    std::shared_ptr<const QUICConnectionCloseFrame> conn_close_frame =
      std::dynamic_pointer_cast<const QUICConnectionCloseFrame>(frame);
    CHECK(conn_close_frame != nullptr);
    CHECK(conn_close_frame->error_code() == QUICTransErrorCode::PROTOCOL_VIOLATION);
    CHECK(conn_close_frame->frame_type() == QUICFrameType::RST_STREAM);
    CHECK(conn_close_frame->reason_phrase_length() == 0);
  }

  SECTION("storing w/ reason phrase")
  {
    uint8_t buf[32];
    size_t len;

    uint8_t expected[] = {
      0x02,                        // Type
      0x00, 0x0A,                  // Error Code
      0x10,                        // Frame Type
      0x05,                        // Reason Phrase Length
      0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
    };
    QUICConnectionCloseFrame connection_close_frame(QUICTransErrorCode::PROTOCOL_VIOLATION, QUICFrameType::STREAM, 5, "ABCDE");
    CHECK(connection_close_frame.size() == sizeof(expected));

    connection_close_frame.store(buf, &len, 32);
    CHECK(len == sizeof(expected));
    CHECK(memcmp(buf, expected, len) == 0);
  }

  SECTION("storing w/o reason phrase")
  {
    uint8_t buf[32];
    size_t len;

    uint8_t expected[] = {
      0x02,       // Type
      0x00, 0x0A, // Error Code
      0x00,       // Frame Type
      0x00,       // Reason Phrase Length
    };
    QUICConnectionCloseFrame connection_close_frame(QUICTransErrorCode::PROTOCOL_VIOLATION, QUICFrameType::UNKNOWN, 0, nullptr);
    connection_close_frame.store(buf, &len, 32);
    CHECK(len == sizeof(expected));
    CHECK(memcmp(buf, expected, len) == 0);
  }
}

TEST_CASE("Load ApplicationClose Frame", "[quic]")
{
  SECTION("w/ reason phrase")
  {
    uint8_t buf1[] = {
      0x03,                        // Type
      0x00, 0x01,                  // Error Code
      0x05,                        // Reason Phrase Length
      0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
    };
    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::APPLICATION_CLOSE);
    CHECK(frame1->size() == 9);

    std::shared_ptr<const QUICApplicationCloseFrame> app_close_frame =
      std::dynamic_pointer_cast<const QUICApplicationCloseFrame>(frame1);
    CHECK(app_close_frame != nullptr);
    CHECK(app_close_frame->error_code() == static_cast<QUICAppErrorCode>(0x01));
    CHECK(app_close_frame->reason_phrase_length() == 5);
    CHECK(memcmp(app_close_frame->reason_phrase(), buf1 + 4, 5) == 0);
  }

  SECTION("w/o reason phrase")
  {
    uint8_t buf2[] = {
      0x03,       // Type
      0x00, 0x01, // Error Code
      0x00,       // Reason Phrase Length
    };
    std::shared_ptr<const QUICFrame> frame2 = QUICFrameFactory::create(buf2, sizeof(buf2));
    CHECK(frame2->type() == QUICFrameType::APPLICATION_CLOSE);
    CHECK(frame2->size() == 4);

    std::shared_ptr<const QUICApplicationCloseFrame> app_close_frame =
      std::dynamic_pointer_cast<const QUICApplicationCloseFrame>(frame2);
    CHECK(app_close_frame != nullptr);
    CHECK(app_close_frame->error_code() == static_cast<QUICAppErrorCode>(0x01));
    CHECK(app_close_frame->reason_phrase_length() == 0);
  }
}

TEST_CASE("Store ApplicationClose Frame", "[quic]")
{
  SECTION("w/ reason phrase")
  {
    uint8_t buf[32];
    size_t len;

    uint8_t expected1[] = {
      0x03,                        // Type
      0x00, 0x01,                  // Error Code
      0x05,                        // Reason Phrase Length
      0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
    };
    QUICApplicationCloseFrame app_close_frame(static_cast<QUICAppErrorCode>(0x01), 5, "ABCDE");
    CHECK(app_close_frame.size() == 9);

    app_close_frame.store(buf, &len, 32);
    CHECK(len == 9);
    CHECK(memcmp(buf, expected1, len) == 0);
  }
  SECTION("w/o reason phrase")
  {
    uint8_t buf[32];
    size_t len;

    uint8_t expected2[] = {
      0x03,       // Type
      0x00, 0x01, // Error Code
      0x00,       // Reason Phrase Length
    };
    QUICApplicationCloseFrame app_close_frame(static_cast<QUICAppErrorCode>(0x01), 0, nullptr);
    CHECK(app_close_frame.size() == 4);

    app_close_frame.store(buf, &len, 32);
    CHECK(len == 4);
    CHECK(memcmp(buf, expected2, len) == 0);
  }
}

TEST_CASE("Load MaxData Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x04,                                          // Type
    0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Data
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::MAX_DATA);
  CHECK(frame1->size() == 9);
  std::shared_ptr<const QUICMaxDataFrame> max_data_frame = std::dynamic_pointer_cast<const QUICMaxDataFrame>(frame1);
  CHECK(max_data_frame != nullptr);
  CHECK(max_data_frame->maximum_data() == 0x1122334455667788ULL);
}

TEST_CASE("Store MaxData Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x04,                                          // Type
    0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Data
  };
  QUICMaxDataFrame max_data_frame(0x1122334455667788);
  CHECK(max_data_frame.size() == 9);

  max_data_frame.store(buf, &len, 65535);
  CHECK(len == 9);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load MaxStreamData Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x05,                                          // Type
    0x81, 0x02, 0x03, 0x04,                        // Stream ID
    0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Stream Data
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::MAX_STREAM_DATA);
  CHECK(frame1->size() == 13);
  std::shared_ptr<const QUICMaxStreamDataFrame> maxStreamDataFrame1 =
    std::dynamic_pointer_cast<const QUICMaxStreamDataFrame>(frame1);
  CHECK(maxStreamDataFrame1 != nullptr);
  CHECK(maxStreamDataFrame1->stream_id() == 0x01020304);
  CHECK(maxStreamDataFrame1->maximum_stream_data() == 0x1122334455667788ULL);
}

TEST_CASE("Store MaxStreamData Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x05,                                          // Type
    0x81, 0x02, 0x03, 0x04,                        // Stream ID
    0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Stream Data
  };
  QUICMaxStreamDataFrame max_stream_data_frame(0x01020304, 0x1122334455667788ULL);
  CHECK(max_stream_data_frame.size() == 13);

  max_stream_data_frame.store(buf, &len, 65535);
  CHECK(len == 13);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load MaxStreamId Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x06,                   // Type
    0x81, 0x02, 0x03, 0x04, // Stream ID
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::MAX_STREAM_ID);
  CHECK(frame1->size() == 5);
  std::shared_ptr<const QUICMaxStreamIdFrame> max_stream_id_frame = std::dynamic_pointer_cast<const QUICMaxStreamIdFrame>(frame1);
  CHECK(max_stream_id_frame != nullptr);
  CHECK(max_stream_id_frame->maximum_stream_id() == 0x01020304);
}

TEST_CASE("Store MaxStreamId Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x06,                   // Type
    0x81, 0x02, 0x03, 0x04, // Stream ID
  };
  QUICMaxStreamIdFrame max_stream_id_frame(0x01020304);
  CHECK(max_stream_id_frame.size() == 5);

  max_stream_id_frame.store(buf, &len, 65535);
  CHECK(len == 5);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load Blocked Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x08, // Type
    0x07, // Offset
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::BLOCKED);
  CHECK(frame1->size() == 2);
  std::shared_ptr<const QUICBlockedFrame> blocked_stream_frame = std::dynamic_pointer_cast<const QUICBlockedFrame>(frame1);
  CHECK(blocked_stream_frame != nullptr);
  CHECK(blocked_stream_frame->offset() == 0x07);
}

TEST_CASE("Store Blocked Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x08, // Type
    0x07, // Offset
  };
  QUICBlockedFrame blocked_stream_frame(0x07);
  CHECK(blocked_stream_frame.size() == 2);

  blocked_stream_frame.store(buf, &len, 65535);
  CHECK(len == 2);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load StreamBlocked Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x09,                   // Type
    0x81, 0x02, 0x03, 0x04, // Stream ID
    0x07,                   // Offset
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::STREAM_BLOCKED);
  CHECK(frame1->size() == 6);
  std::shared_ptr<const QUICStreamBlockedFrame> stream_blocked_frame =
    std::dynamic_pointer_cast<const QUICStreamBlockedFrame>(frame1);
  CHECK(stream_blocked_frame != nullptr);
  CHECK(stream_blocked_frame->stream_id() == 0x01020304);
  CHECK(stream_blocked_frame->offset() == 0x07);
}

TEST_CASE("Store StreamBlocked Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x09,                   // Type
    0x81, 0x02, 0x03, 0x04, // Stream ID
    0x07,                   // Offset
  };
  QUICStreamBlockedFrame stream_blocked_frame(0x01020304, 0x07);
  CHECK(stream_blocked_frame.size() == 6);

  stream_blocked_frame.store(buf, &len, 65535);
  CHECK(len == 6);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load StreamIdBlocked Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x0a,       // Type
    0x41, 0x02, // Stream ID
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::STREAM_ID_BLOCKED);
  CHECK(frame1->size() == 3);
  std::shared_ptr<const QUICStreamIdBlockedFrame> stream_id_blocked_frame =
    std::dynamic_pointer_cast<const QUICStreamIdBlockedFrame>(frame1);
  CHECK(stream_id_blocked_frame != nullptr);
  CHECK(stream_id_blocked_frame->stream_id() == 0x0102);
}

TEST_CASE("Store StreamIdBlocked Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x0a,       // Type
    0x41, 0x02, // Stream ID
  };
  QUICStreamIdBlockedFrame stream_id_blocked_frame(0x0102);
  CHECK(stream_id_blocked_frame.size() == 3);

  stream_id_blocked_frame.store(buf, &len, 65535);
  CHECK(len == 3);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load NewConnectionId Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x0b,                                           // Type
    0x41, 0x02,                                     // Sequence
    0x08,                                           // Length
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, // Connection ID
    0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, // Stateless Reset Token
    0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::NEW_CONNECTION_ID);
  CHECK(frame1->size() == 28);
  std::shared_ptr<const QUICNewConnectionIdFrame> new_con_id_frame =
    std::dynamic_pointer_cast<const QUICNewConnectionIdFrame>(frame1);
  CHECK(new_con_id_frame != nullptr);
  CHECK(new_con_id_frame->sequence() == 0x0102);
  CHECK((new_con_id_frame->connection_id() ==
         QUICConnectionId(reinterpret_cast<const uint8_t *>("\x11\x22\x33\x44\x55\x66\x77\x88"), 8)));
  CHECK(memcmp(new_con_id_frame->stateless_reset_token().buf(), buf1 + 12, 16) == 0);
}

TEST_CASE("Store NewConnectionId Frame", "[quic]")
{
  uint8_t buf[32];
  size_t len;

  uint8_t expected[] = {
    0x0b,                                           // Type
    0x41, 0x02,                                     // Sequence
    0x08,                                           // Length
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, // Connection ID
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Stateless Reset Token
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  };
  QUICNewConnectionIdFrame new_con_id_frame(0x0102, {reinterpret_cast<const uint8_t *>("\x11\x22\x33\x44\x55\x66\x77\x88"), 8},
                                            {expected + 12});
  CHECK(new_con_id_frame.size() == 28);

  new_con_id_frame.store(buf, &len, 32);
  CHECK(len == 28);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load STOP_SENDING Frame", "[quic]")
{
  uint8_t buf[] = {
    0x0c,                   // Type
    0x92, 0x34, 0x56, 0x78, // Stream ID
    0x00, 0x01,             // Error Code
  };
  std::shared_ptr<const QUICFrame> frame = QUICFrameFactory::create(buf, sizeof(buf));
  CHECK(frame->type() == QUICFrameType::STOP_SENDING);
  CHECK(frame->size() == 7);

  std::shared_ptr<const QUICStopSendingFrame> stop_sending_frame = std::dynamic_pointer_cast<const QUICStopSendingFrame>(frame);
  CHECK(stop_sending_frame != nullptr);
  CHECK(stop_sending_frame->stream_id() == 0x12345678);
  CHECK(stop_sending_frame->error_code() == 0x0001);
}

TEST_CASE("Store STOP_SENDING Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x0c,                   // Type
    0x92, 0x34, 0x56, 0x78, // Stream ID
    0x00, 0x01,             // Error Code
  };
  QUICStopSendingFrame stop_sending_frame(0x12345678, static_cast<QUICAppErrorCode>(0x01));
  CHECK(stop_sending_frame.size() == 7);

  stop_sending_frame.store(buf, &len, 65535);
  CHECK(len == 7);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load PATH_CHALLENGE Frame", "[quic]")
{
  uint8_t buf[] = {
    0x0e,                                           // Type
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, // Data
  };
  std::shared_ptr<const QUICFrame> frame = QUICFrameFactory::create(buf, sizeof(buf));
  CHECK(frame->type() == QUICFrameType::PATH_CHALLENGE);
  CHECK(frame->size() == 9);

  std::shared_ptr<const QUICPathChallengeFrame> path_challenge_frame =
    std::dynamic_pointer_cast<const QUICPathChallengeFrame>(frame);
  CHECK(path_challenge_frame != nullptr);
  CHECK(memcmp(path_challenge_frame->data(), "\x01\x23\x45\x67\x89\xab\xcd\xef", QUICPathChallengeFrame::DATA_LEN) == 0);
}

TEST_CASE("Store PATH_CHALLENGE Frame", "[quic]")
{
  uint8_t buf[16];
  size_t len;

  uint8_t expected[] = {
    0x0e,                                           // Type
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, // Data
  };

  uint8_t raw[]       = "\x01\x23\x45\x67\x89\xab\xcd\xef";
  size_t raw_len      = sizeof(raw) - 1;
  ats_unique_buf data = ats_unique_malloc(raw_len);
  memcpy(data.get(), raw, raw_len);

  QUICPathChallengeFrame frame(std::move(data), QUICPathChallengeFrame::DATA_LEN);
  CHECK(frame.size() == 9);

  frame.store(buf, &len, 16);
  CHECK(len == 9);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load PATH_RESPONSE Frame", "[quic]")
{
  uint8_t buf[] = {
    0x0f,                                           // Type
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, // Data
  };
  std::shared_ptr<const QUICFrame> frame = QUICFrameFactory::create(buf, sizeof(buf));
  CHECK(frame->type() == QUICFrameType::PATH_RESPONSE);
  CHECK(frame->size() == 9);

  std::shared_ptr<const QUICPathResponseFrame> path_response_frame = std::dynamic_pointer_cast<const QUICPathResponseFrame>(frame);
  CHECK(path_response_frame != nullptr);
  CHECK(memcmp(path_response_frame->data(), "\x01\x23\x45\x67\x89\xab\xcd\xef", QUICPathResponseFrame::DATA_LEN) == 0);
}

TEST_CASE("Store PATH_RESPONSE Frame", "[quic]")
{
  uint8_t buf[16];
  size_t len;

  uint8_t expected[] = {
    0x0f,                                           // Type
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, // Data
  };

  uint8_t raw[]       = "\x01\x23\x45\x67\x89\xab\xcd\xef";
  size_t raw_len      = sizeof(raw) - 1;
  ats_unique_buf data = ats_unique_malloc(raw_len);
  memcpy(data.get(), raw, raw_len);

  QUICPathResponseFrame frame(std::move(data), QUICPathResponseFrame::DATA_LEN);
  CHECK(frame.size() == 9);

  frame.store(buf, &len, 16);
  CHECK(len == 9);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("NEW_TOKEN Frame", "[quic]")
{
  uint8_t raw_new_token_frame[] = {
    0x19,                                           // Type
    0x08,                                           // Token Length (i)
    0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, // Token (*)
  };
  size_t raw_new_token_frame_len = sizeof(raw_new_token_frame);

  uint8_t raw_token[]  = {0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef, 0xbe, 0xef};
  size_t raw_token_len = sizeof(raw_token);

  SECTION("load")
  {
    std::shared_ptr<const QUICFrame> frame = QUICFrameFactory::create(raw_new_token_frame, raw_new_token_frame_len);
    CHECK(frame->type() == QUICFrameType::NEW_TOKEN);
    CHECK(frame->size() == raw_new_token_frame_len);

    std::shared_ptr<const QUICNewTokenFrame> new_token_frame = std::dynamic_pointer_cast<const QUICNewTokenFrame>(frame);
    CHECK(new_token_frame != nullptr);
    CHECK(new_token_frame->token_length() == raw_token_len);
    CHECK(memcmp(new_token_frame->token(), raw_token, raw_token_len) == 0);
  }

  SECTION("store")
  {
    uint8_t buf[32];
    size_t len;

    ats_unique_buf token = ats_unique_malloc(raw_token_len);
    memcpy(token.get(), raw_token, raw_token_len);

    QUICNewTokenFrame frame(std::move(token), raw_token_len);
    CHECK(frame.size() == raw_new_token_frame_len);

    frame.store(buf, &len, 16);
    CHECK(len == raw_new_token_frame_len);
    CHECK(memcmp(buf, raw_new_token_frame, len) == 0);
  }
}

TEST_CASE("QUICFrameFactory Create Unknown Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x20, // Type
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1 == nullptr);
}

TEST_CASE("QUICFrameFactory Fast Create Frame", "[quic]")
{
  QUICFrameFactory factory;

  uint8_t buf1[] = {
    0x06,                   // Type
    0x81, 0x02, 0x03, 0x04, // Stream Data
  };
  uint8_t buf2[] = {
    0x06,                   // Type
    0x85, 0x06, 0x07, 0x08, // Stream Data
  };
  std::shared_ptr<const QUICFrame> frame1 = factory.fast_create(buf1, sizeof(buf1));
  CHECK(frame1 != nullptr);

  std::shared_ptr<const QUICMaxStreamIdFrame> max_stream_id_frame1 = std::dynamic_pointer_cast<const QUICMaxStreamIdFrame>(frame1);
  CHECK(max_stream_id_frame1 != nullptr);
  CHECK(max_stream_id_frame1->maximum_stream_id() == 0x01020304);

  std::shared_ptr<const QUICFrame> frame2 = factory.fast_create(buf2, sizeof(buf2));
  CHECK(frame2 != nullptr);

  std::shared_ptr<const QUICMaxStreamIdFrame> max_stream_id_frame2 = std::dynamic_pointer_cast<const QUICMaxStreamIdFrame>(frame2);
  CHECK(max_stream_id_frame2 != nullptr);
  CHECK(max_stream_id_frame2->maximum_stream_id() == 0x05060708);

  CHECK(frame1 == frame2);
}

TEST_CASE("QUICFrameFactory Fast Create Unknown Frame", "[quic]")
{
  QUICFrameFactory factory;

  uint8_t buf1[] = {
    0x20, // Type
  };
  std::shared_ptr<const QUICFrame> frame1 = factory.fast_create(buf1, sizeof(buf1));
  CHECK(frame1 == nullptr);
}

TEST_CASE("QUICFrameFactory Create CONNECTION_CLOSE with a QUICConnectionError", "[quic]")
{
  std::unique_ptr<QUICConnectionError> error =
    std::unique_ptr<QUICConnectionError>(new QUICConnectionError(QUICTransErrorCode::INTERNAL_ERROR));
  std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc> connection_close_frame1 =
    QUICFrameFactory::create_connection_close_frame(std::move(error));
  CHECK(connection_close_frame1->error_code() == QUICTransErrorCode::INTERNAL_ERROR);
  CHECK(connection_close_frame1->reason_phrase_length() == 0);
  CHECK(connection_close_frame1->reason_phrase() == nullptr);

  error = std::unique_ptr<QUICConnectionError>(new QUICConnectionError(QUICTransErrorCode::INTERNAL_ERROR, "test"));
  std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc> connection_close_frame2 =
    QUICFrameFactory::create_connection_close_frame(std::move(error));
  CHECK(connection_close_frame2->error_code() == QUICTransErrorCode::INTERNAL_ERROR);
  CHECK(connection_close_frame2->reason_phrase_length() == 4);
  CHECK(memcmp(connection_close_frame2->reason_phrase(), "test", 4) == 0);
}

TEST_CASE("QUICFrameFactory Create RST_STREAM with a QUICStreamError", "[quic]")
{
  QUICStream stream(new MockQUICRTTProvider(), new MockQUICConnection(), 0x1234, 0, 0);
  std::unique_ptr<QUICStreamError> error =
    std::unique_ptr<QUICStreamError>(new QUICStreamError(&stream, static_cast<QUICAppErrorCode>(0x01)));
  std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc> rst_stream_frame1 =
    QUICFrameFactory::create_rst_stream_frame(std::move(error));
  CHECK(rst_stream_frame1->error_code() == 0x01);
  CHECK(rst_stream_frame1->stream_id() == 0x1234);
  CHECK(rst_stream_frame1->final_offset() == 0);
}

// Test for retransmittable frames
TEST_CASE("Retransmit", "[quic][frame][retransmit]")
{
  QUICPacketFactory factory;
  MockQUICHandshakeProtocol hs_protocol;
  factory.set_hs_protocol(&hs_protocol);
  QUICPacketUPtr packet = factory.create_server_protected_packet({reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04"), 4}, 0,
                                                                 {nullptr, [](void *p) { ats_free(p); }}, 0, true);
  SECTION("STREAM frame split")
  {
    size_t len;
    uint8_t buf1[] = {
      0x16,                                           // 0b00010OLF (OLF=110)
      0x01,                                           // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Offset
      0x0a,                                           // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05,                   // Stream Data
      0x11, 0x22, 0x33, 0x44, 0x55,
    };

    uint8_t expected[]{
      0x16,                                           // 0b00010OLF (OLF=110)
      0x01,                                           // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, // Offset
      0x05,                                           // Data Length
      0x11, 0x22, 0x33, 0x44, 0x55,                   // Stream Data
    };

    uint8_t expected2[]{
      0x16,                                           // 0b00010OLF (OLF=110)
      0x01,                                           // Stream ID
      0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, // Offset
      0x05,                                           // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05,                   // Stream Data
    };

    uint8_t buffer[128];

    QUICFrameUPtr frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    auto frame           = QUICFrameFactory::create_retransmission_frame(std::move(frame1), *packet);
    auto frame2          = frame->split(16);

    CHECK(frame2->store(buffer, &len, 128));
    CHECK(memcmp(buffer, expected, sizeof(expected)) == 0);
    CHECK(frame->store(buffer, &len, 128));
    CHECK(memcmp(buffer, expected2, sizeof(expected2)) == 0);
  }

  SECTION("STREAM frame")
  {
    uint8_t frame_buf[] = {
      0x12,                         // Type, 0b00010OLF (OLF=010)
      0x01,                         // Stream ID
      0x05,                         // Data Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Stream Data
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 8);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("CRYPTO frame")
  {
    uint8_t frame_buf[] = {
      0x18,                         // Type
      0x01,                         // Offset
      0x05,                         // Length
      0x01, 0x02, 0x03, 0x04, 0x05, // Crypto Data
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 8);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("RST_STREAM frame")
  {
    uint8_t frame_buf[] = {
      0x01,                                          // Type
      0x92, 0x34, 0x56, 0x78,                        // Stream ID
      0x00, 0x01,                                    // Error Code
      0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Final Offset
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 15);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("CONNECTION_CLOSE frame")
  {
    uint8_t frame_buf[] = {
      0x02,                        // Type
      0x00, 0x0A,                  // Error Code
      0x00,                        // Frame Type
      0x05,                        // Reason Phrase Length
      0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == sizeof(frame_buf));
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("APPLICATION_CLOSE frame")
  {
    uint8_t frame_buf[] = {
      0x03,                        // Type
      0x00, 0x01,                  // Error Code
      0x05,                        // Reason Phrase Length
      0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 9);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("MAX_DATA frame")
  {
    uint8_t frame_buf[] = {
      0x04,                                          // Type
      0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Data
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 9);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("MAX_STREAM_DATA frame")
  {
    uint8_t frame_buf[] = {
      0x05,                                          // Type
      0x81, 0x02, 0x03, 0x04,                        // Stream ID
      0xd1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Stream Data
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 13);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("MAX_STREAM_ID frame")
  {
    uint8_t frame_buf[] = {
      0x06,                   // Type
      0x81, 0x02, 0x03, 0x04, // Stream ID
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 5);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("PING frame")
  {
    uint8_t frame_buf[] = {
      0x07, // Type
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 1);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("BLOCKED frame")
  {
    uint8_t frame_buf[] = {
      0x08, // Type
      0x07, // Offset
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 2);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("STREAM_BLOCKED frame")
  {
    uint8_t frame_buf[] = {
      0x09,                   // Type
      0x81, 0x02, 0x03, 0x04, // Stream ID
      0x07,                   // Offset
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 6);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("STREAM_ID_BLOCKED frame")
  {
    uint8_t frame_buf[] = {
      0x0a,       // Type
      0x41, 0x02, // Stream ID
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 3);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("NEW_CONNECTION_ID frame")
  {
    uint8_t frame_buf[] = {
      0x0b,                                           // Type
      0x41, 0x02,                                     // Sequence
      0x08,                                           // Length
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, // Connection ID
      0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, // Stateless Reset Token
      0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 28);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("STOP_SENDING frame")
  {
    uint8_t frame_buf[] = {
      0x0c,                   // Type
      0x92, 0x34, 0x56, 0x78, // Stream ID
      0x00, 0x01,             // Error Code
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 7);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("PATH_CHALLENGE frame")
  {
    uint8_t frame_buf[] = {
      0x0e,                                           // Type
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, // Data
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 9);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }

  SECTION("PATH_RESPONSE frame")
  {
    uint8_t frame_buf[] = {
      0x0f,                                           // Type
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, // Data
    };

    QUICFrameUPtr frame = QUICFrameFactory::create(frame_buf, sizeof(frame_buf));
    frame               = QUICFrameFactory::create_retransmission_frame(std::move(frame), *packet);

    uint8_t buf[32] = {0};
    size_t len;
    frame->store(buf, &len, 32);

    CHECK(len == 9);
    CHECK(memcmp(buf, frame_buf, len) == 0);
  }
}
