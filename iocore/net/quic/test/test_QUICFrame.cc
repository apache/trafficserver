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
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0a")) == QUICFrameType::STREAM_ID_NEEDED);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0b")) == QUICFrameType::NEW_CONNECTION_ID);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0c")) == QUICFrameType::STOP_SENDING);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0d")) == QUICFrameType::PONG);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0e")) == QUICFrameType::ACK);
  // Undefined ragne
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x0f")) == QUICFrameType::UNKNOWN);
  // Range of STREAM
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x10")) == QUICFrameType::STREAM);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x17")) == QUICFrameType::STREAM);
  // Undefined ragne
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\x18")) == QUICFrameType::UNKNOWN);
  CHECK(QUICFrame::type(reinterpret_cast<const uint8_t *>("\xff")) == QUICFrameType::UNKNOWN);
}

TEST_CASE("Load STREAM Frame 1", "[quic]")
{
  uint8_t buf1[] = {
    0x10,                   // 0b00010OLF (OLF=000)
    0x01,                   // Stream ID
    0x01, 0x02, 0x03, 0x04, // Stream Data
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::STREAM);
  CHECK(frame1->size() == 6);
  std::shared_ptr<const QUICStreamFrame> streamFrame1 = std::dynamic_pointer_cast<const QUICStreamFrame>(frame1);
  CHECK(streamFrame1->stream_id() == 0x01);
  CHECK(streamFrame1->offset() == 0x00);
  CHECK(streamFrame1->data_length() == 4);
  CHECK(memcmp(streamFrame1->data(), "\x01\x02\x03\x04", 4) == 0);
  CHECK(streamFrame1->has_fin_flag() == false);
}

TEST_CASE("Load STREAM Frame 2", "[quic]")
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
  std::shared_ptr<const QUICStreamFrame> streamFrame1 = std::dynamic_pointer_cast<const QUICStreamFrame>(frame1);
  CHECK(streamFrame1->stream_id() == 0x01);
  CHECK(streamFrame1->offset() == 0x00);
  CHECK(streamFrame1->data_length() == 5);
  CHECK(memcmp(streamFrame1->data(), "\x01\x02\x03\x04\x05", 5) == 0);
  CHECK(streamFrame1->has_fin_flag() == false);
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

    QUICStreamFrame streamFrame1(std::move(payload1), 5, 0x01, 0x00);
    streamFrame1.store(buf, &len);
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

    QUICStreamFrame streamFrame2(std::move(payload2), 5, 0x01, 0x01);
    streamFrame2.store(buf, &len);
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

    QUICStreamFrame streamFrame3(std::move(payload3), 5, 0x01, 0x010000);
    streamFrame3.store(buf, &len);
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

    QUICStreamFrame streamFrame4(std::move(payload4), 5, 0x01, 0x0100000000);
    streamFrame4.store(buf, &len);
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

    QUICStreamFrame streamFrame5(std::move(payload5), 5, 0x0100, 0x0100000000);
    streamFrame5.store(buf, &len);
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

    QUICStreamFrame streamFrame6(std::move(payload6), 5, 0x010000, 0x0100000000);
    streamFrame6.store(buf, &len);
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

    QUICStreamFrame streamFrame7(std::move(payload7), 5, 0x01000000, 0x0100000000);
    streamFrame7.store(buf, &len);
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

    QUICStreamFrame streamFrame(std::move(payload), 5, 0x01000000, 0x0100000000, true);
    streamFrame.store(buf, &len);
    CHECK(len == 19);
    CHECK(memcmp(buf, expected, len) == 0);
  }
}

TEST_CASE("Load Ack Frame 1", "[quic]")
{
  SECTION("0 Ack Block, 8 bit packet number length, 8 bit block length")
  {
    uint8_t buf1[] = {
      0xA0,       // 101NLLMM
      0x12,       // Largest Acknowledged
      0x34, 0x56, // Ack Delay
      0x00,       // Ack Block Section
    };
    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::ACK);
    CHECK(frame1->size() == 5);
    std::shared_ptr<const QUICAckFrame> ackFrame1 = std::dynamic_pointer_cast<const QUICAckFrame>(frame1);
    CHECK(ackFrame1 != nullptr);
    CHECK(ackFrame1->has_ack_blocks() == false);
    CHECK(ackFrame1->largest_acknowledged() == 0x12);
    CHECK(ackFrame1->ack_delay() == 0x3456);
  }

  SECTION("2 Ack Block, 8 bit packet number length, 8 bit block length")
  {
    uint8_t buf1[] = {
      0xB0,       // 101NLLMM
      0x02,       // Num Blocks
      0x12,       // Largest Acknowledged
      0x34, 0x56, // Ack Delay
      0x01,       // Ack Block Section (First ACK Block Length)
      0x02,       // Ack Block Section (Gap 1)
      0x03,       // Ack Block Section (ACK Block 1 Length)
      0x04,       // Ack Block Section (Gap 2)
      0x05,       // Ack Block Section (ACK Block 2 Length)
    };

    std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
    CHECK(frame1->type() == QUICFrameType::ACK);
    CHECK(frame1->size() == 10);
    std::shared_ptr<const QUICAckFrame> ackFrame1 = std::dynamic_pointer_cast<const QUICAckFrame>(frame1);
    CHECK(ackFrame1 != nullptr);
    CHECK(ackFrame1->largest_acknowledged() == 0x12);
    CHECK(ackFrame1->ack_delay() == 0x3456);
    CHECK(ackFrame1->num_blocks() == 2);
    CHECK(ackFrame1->has_ack_blocks() == true);
    const QUICAckFrame::AckBlockSection *section = ackFrame1->ack_block_section();
    CHECK(section->first_ack_block_length() == 0x01);
    auto ite = section->begin();
    CHECK(ite != section->end());
    CHECK(ite->gap() == 2);
    CHECK(ite->length() == 3);
    ++ite;
    CHECK(ite != section->end());
    CHECK(ite->gap() == 4);
    CHECK(ite->length() == 5);
    ++ite;
    CHECK(ite == section->end());
  }
}

TEST_CASE("Load Ack Frame 2", "[quic]")
{
  // 0 Ack Block, 8 bit packet number length, 8 bit block length
  uint8_t buf1[] = {
    0xAA,                   // 101NLLMM '0b10101010' { N: 0, LL: 10, MM:10 }
    0x00, 0x00, 0x00, 0x01, // Largest Acknowledged
    0x01, 0x71,             // Ack Delay
    0x00, 0x00, 0x00, 0x01, // ACK Block
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::ACK);
  CHECK(frame1->size() == 11);
  std::shared_ptr<const QUICAckFrame> ackFrame1 = std::dynamic_pointer_cast<const QUICAckFrame>(frame1);
  CHECK(ackFrame1 != nullptr);
  CHECK(ackFrame1->has_ack_blocks() == false);
  CHECK(ackFrame1->largest_acknowledged() == 0x01);
  CHECK(ackFrame1->ack_delay() == 0x0171);

  // TODO: 1 Ack Block
}

TEST_CASE("Store Ack Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  // 0 Ack Block, 8 bit packet number length, 8 bit block length
  uint8_t expected[] = {
    0xA2,                   // 101NLLMM
    0x12,                   // Largest Acknowledged
    0x34, 0x56,             // Ack Delay
    0x00, 0x00, 0x00, 0x00, // Ack Block Section
  };
  QUICAckFrame ackFrame(0x12, 0x3456, 0);
  ackFrame.store(buf, &len);
  CHECK(len == 8);
  CHECK(memcmp(buf, expected, len) == 0);

  // TODO: Add ack blocks
}

TEST_CASE("Load RST_STREAM Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x01,                                          // Type
    0x12, 0x34, 0x56, 0x78,                        // Stream ID
    0x00, 0x01,                                    // Error Code
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Final Offset
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
    0x12, 0x34, 0x56, 0x78,                        // Stream ID
    0x00, 0x01,                                    // Error Code
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Final Offset
  };
  QUICRstStreamFrame rst_stream_frame(0x12345678, 0x0001, 0x1122334455667788);
  rst_stream_frame.store(buf, &len);
  CHECK(len == 15);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load Ping Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x07, // Type
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::PING);
  CHECK(frame1->size() == 1);
  std::shared_ptr<const QUICPingFrame> pingStreamFrame1 = std::dynamic_pointer_cast<const QUICPingFrame>(frame1);
  CHECK(pingStreamFrame1 != nullptr);
}

TEST_CASE("Store Ping Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x07, // Type
  };
  QUICPingFrame pingStreamFrame;
  pingStreamFrame.store(buf, &len);
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
  QUICPaddingFrame paddingFrame;
  paddingFrame.store(buf, &len);
  CHECK(len == 1);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load ConnectionClose Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x02,                        // Type
    0x00, 0x0A,                  // Error Code
    0x00, 0x05,                  // Reason Phrase Length
    0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::CONNECTION_CLOSE);
  CHECK(frame1->size() == 10);
  std::shared_ptr<const QUICConnectionCloseFrame> connectionCloseFrame1 =
    std::dynamic_pointer_cast<const QUICConnectionCloseFrame>(frame1);
  CHECK(connectionCloseFrame1 != nullptr);
  CHECK(connectionCloseFrame1->error_code() == QUICTransErrorCode::PROTOCOL_VIOLATION);
  CHECK(connectionCloseFrame1->reason_phrase_length() == 5);
  CHECK(memcmp(connectionCloseFrame1->reason_phrase(), buf1 + 5, 5) == 0);

  // No reason phrase
  uint8_t buf2[] = {
    0x02,       // Type
    0x00, 0x0A, // Error Code
    0x00, 0x00, // Reason Phrase Length
  };
  std::shared_ptr<const QUICFrame> frame2 = QUICFrameFactory::create(buf2, sizeof(buf1));
  CHECK(frame2->type() == QUICFrameType::CONNECTION_CLOSE);
  CHECK(frame2->size() == 5);
  std::shared_ptr<const QUICConnectionCloseFrame> connectionCloseFrame2 =
    std::dynamic_pointer_cast<const QUICConnectionCloseFrame>(frame2);
  CHECK(connectionCloseFrame2 != nullptr);
  CHECK(connectionCloseFrame2->error_code() == QUICTransErrorCode::PROTOCOL_VIOLATION);
  CHECK(connectionCloseFrame2->reason_phrase_length() == 0);
}

TEST_CASE("Store ConnectionClose Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected1[] = {
    0x02,                        // Type
    0x00, 0x0A,                  // Error Code
    0x00, 0x05,                  // Reason Phrase Length
    0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
  };
  QUICConnectionCloseFrame connectionCloseFrame1(QUICTransErrorCode::PROTOCOL_VIOLATION, 5, "ABCDE");
  connectionCloseFrame1.store(buf, &len);
  CHECK(len == 10);
  CHECK(memcmp(buf, expected1, len) == 0);

  uint8_t expected2[] = {
    0x02,       // Type
    0x00, 0x0A, // Error Code
    0x00, 0x00, // Reason Phrase Length
  };
  QUICConnectionCloseFrame connectionCloseFrame2(QUICTransErrorCode::PROTOCOL_VIOLATION, 0, nullptr);
  connectionCloseFrame2.store(buf, &len);
  CHECK(len == 5);
  CHECK(memcmp(buf, expected2, len) == 0);
}

TEST_CASE("Load ApplicationClose Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x03,                        // Type
    0x00, 0x01,                  // Error Code
    0x00, 0x05,                  // Reason Phrase Length
    0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::APPLICATION_CLOSE);
  CHECK(frame1->size() == 10);
  std::shared_ptr<const QUICApplicationCloseFrame> applicationCloseFrame1 =
    std::dynamic_pointer_cast<const QUICApplicationCloseFrame>(frame1);
  CHECK(applicationCloseFrame1 != nullptr);
  CHECK(applicationCloseFrame1->error_code() == static_cast<QUICAppErrorCode>(0x01));
  CHECK(applicationCloseFrame1->reason_phrase_length() == 5);
  CHECK(memcmp(applicationCloseFrame1->reason_phrase(), buf1 + 5, 5) == 0);

  // No reason phrase
  uint8_t buf2[] = {
    0x03,       // Type
    0x00, 0x01, // Error Code
    0x00, 0x00, // Reason Phrase Length
  };
  std::shared_ptr<const QUICFrame> frame2 = QUICFrameFactory::create(buf2, sizeof(buf1));
  CHECK(frame2->type() == QUICFrameType::APPLICATION_CLOSE);
  CHECK(frame2->size() == 5);
  std::shared_ptr<const QUICApplicationCloseFrame> applicationCloseFrame2 =
    std::dynamic_pointer_cast<const QUICApplicationCloseFrame>(frame2);
  CHECK(applicationCloseFrame2 != nullptr);
  CHECK(applicationCloseFrame2->error_code() == static_cast<QUICAppErrorCode>(0x01));
  CHECK(applicationCloseFrame2->reason_phrase_length() == 0);
}

TEST_CASE("Store ApplicationClose Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected1[] = {
    0x03,                        // Type
    0x00, 0x01,                  // Error Code
    0x00, 0x05,                  // Reason Phrase Length
    0x41, 0x42, 0x43, 0x44, 0x45 // Reason Phrase ("ABCDE");
  };
  QUICApplicationCloseFrame applicationCloseFrame1(static_cast<QUICAppErrorCode>(0x01), 5, "ABCDE");
  applicationCloseFrame1.store(buf, &len);
  CHECK(len == 10);
  CHECK(memcmp(buf, expected1, len) == 0);

  uint8_t expected2[] = {
    0x03,       // Type
    0x00, 0x01, // Error Code
    0x00, 0x00, // Reason Phrase Length
  };
  QUICApplicationCloseFrame applicationCloseFrame2(static_cast<QUICAppErrorCode>(0x01), 0, nullptr);
  applicationCloseFrame2.store(buf, &len);
  CHECK(len == 5);
  CHECK(memcmp(buf, expected2, len) == 0);
}

TEST_CASE("Load MaxData Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x04,                                          // Type
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Data
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::MAX_DATA);
  CHECK(frame1->size() == 9);
  std::shared_ptr<const QUICMaxDataFrame> maxDataFrame1 = std::dynamic_pointer_cast<const QUICMaxDataFrame>(frame1);
  CHECK(maxDataFrame1 != nullptr);
  CHECK(maxDataFrame1->maximum_data() == 0x1122334455667788ULL);
}

TEST_CASE("Store MaxData Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x04,                                          // Type
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Data
  };
  QUICMaxDataFrame maxDataFrame(0x1122334455667788);
  maxDataFrame.store(buf, &len);
  CHECK(len == 9);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load MaxStreamData Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x05,                                          // Type
    0x01, 0x02, 0x03, 0x04,                        // Stream ID
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Stream Data
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
    0x01, 0x02, 0x03, 0x04,                        // Stream ID
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 // Maximum Stream Data
  };
  QUICMaxStreamDataFrame maxStreamDataFrame(0x01020304, 0x1122334455667788ULL);
  maxStreamDataFrame.store(buf, &len);
  CHECK(len == 13);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load MaxStreamId Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x06,                   // Type
    0x01, 0x02, 0x03, 0x04, // Stream ID
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::MAX_STREAM_ID);
  CHECK(frame1->size() == 5);
  std::shared_ptr<const QUICMaxStreamIdFrame> maxStreamIdFrame1 = std::dynamic_pointer_cast<const QUICMaxStreamIdFrame>(frame1);
  CHECK(maxStreamIdFrame1 != nullptr);
  CHECK(maxStreamIdFrame1->maximum_stream_id() == 0x01020304);
}

TEST_CASE("Store MaxStreamId Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x06,                   // Type
    0x01, 0x02, 0x03, 0x04, // Stream ID
  };
  QUICMaxStreamIdFrame maxStreamIdFrame(0x01020304);
  maxStreamIdFrame.store(buf, &len);
  CHECK(len == 5);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load Blocked Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x08, // Type
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::BLOCKED);
  CHECK(frame1->size() == 1);
  std::shared_ptr<const QUICBlockedFrame> blockedStreamFrame1 = std::dynamic_pointer_cast<const QUICBlockedFrame>(frame1);
  CHECK(blockedStreamFrame1 != nullptr);
}

TEST_CASE("Store Blocked Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x08, // Type
  };
  QUICBlockedFrame blockedStreamFrame;
  blockedStreamFrame.store(buf, &len);
  CHECK(len == 1);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load StreamBlocked Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x09,                   // Type
    0x01, 0x02, 0x03, 0x04, // Stream ID
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::STREAM_BLOCKED);
  CHECK(frame1->size() == 5);
  std::shared_ptr<const QUICStreamBlockedFrame> streamBlockedFrame1 =
    std::dynamic_pointer_cast<const QUICStreamBlockedFrame>(frame1);
  CHECK(streamBlockedFrame1 != nullptr);
  CHECK(streamBlockedFrame1->stream_id() == 0x01020304);
}

TEST_CASE("Store StreamBlocked Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x09,                   // Type
    0x01, 0x02, 0x03, 0x04, // Stream ID
  };
  QUICStreamBlockedFrame streamBlockedFrame(0x01020304);
  streamBlockedFrame.store(buf, &len);
  CHECK(len == 5);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load StreamIdNeeded Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x0a, // Type
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::STREAM_ID_NEEDED);
  CHECK(frame1->size() == 1);
  std::shared_ptr<const QUICStreamIdNeededFrame> streamIdNeededFrame1 =
    std::dynamic_pointer_cast<const QUICStreamIdNeededFrame>(frame1);
  CHECK(streamIdNeededFrame1 != nullptr);
}

TEST_CASE("Store StreamIdNeeded Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {
    0x0a, // Type
  };
  QUICStreamIdNeededFrame streamIdNeededStreamFrame;
  streamIdNeededStreamFrame.store(buf, &len);
  CHECK(len == 1);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load NewConnectionId Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x0b,                                           // Type
    0x01, 0x02,                                     // Sequence
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, // Connection ID
    0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, // Stateless Reset Token
    0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1->type() == QUICFrameType::NEW_CONNECTION_ID);
  CHECK(frame1->size() == 11);
  std::shared_ptr<const QUICNewConnectionIdFrame> newConnectionIdFrame1 =
    std::dynamic_pointer_cast<const QUICNewConnectionIdFrame>(frame1);
  CHECK(newConnectionIdFrame1 != nullptr);
  CHECK(newConnectionIdFrame1->sequence() == 0x0102);
  CHECK(newConnectionIdFrame1->connection_id() == 0x1122334455667788ULL);
  CHECK(memcmp(newConnectionIdFrame1->stateless_reset_token().buf(), buf1 + 11, 16) == 0);
}

TEST_CASE("Store NewConnectionId Frame", "[quic]")
{
  uint8_t buf[65535];
  size_t len;

  uint8_t expected[] = {0x0b,                                           // Type
                        0x01, 0x02,                                     // Sequence
                        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, // Connection ID
                        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  QUICNewConnectionIdFrame newConnectionIdFrame(0x0102, 0x1122334455667788ULL, {expected + 11});
  newConnectionIdFrame.store(buf, &len);
  CHECK(len == 27);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("Load STOP_SENDING Frame", "[quic]")
{
  uint8_t buf[] = {
    0x0c,                   // Type
    0x12, 0x34, 0x56, 0x78, // Stream ID
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
    0x12, 0x34, 0x56, 0x78, // Stream ID
    0x00, 0x01,             // Error Code
  };
  QUICStopSendingFrame stop_sending_frame(0x12345678, static_cast<QUICAppErrorCode>(0x01));
  stop_sending_frame.store(buf, &len);
  CHECK(len == 7);
  CHECK(memcmp(buf, expected, len) == 0);
}

TEST_CASE("QUICFrameFactory Create Unknown Frame", "[quic]")
{
  uint8_t buf1[] = {
    0x0f, // Type
  };
  std::shared_ptr<const QUICFrame> frame1 = QUICFrameFactory::create(buf1, sizeof(buf1));
  CHECK(frame1 == nullptr);
}

TEST_CASE("QUICFrameFactory Fast Create Frame", "[quic]")
{
  QUICFrameFactory factory;

  uint8_t buf1[] = {
    0x06,                   // Type
    0x01, 0x02, 0x03, 0x04, // Stream Data
  };
  uint8_t buf2[] = {
    0x06,                   // Type
    0x05, 0x06, 0x07, 0x08, // Stream Data
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
    0x0f, // Type
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
  QUICStream stream;
  stream.init(new MockQUICFrameTransmitter(), 0, 0x1234, 0, 0);
  std::unique_ptr<QUICStreamError> error =
    std::unique_ptr<QUICStreamError>(new QUICStreamError(&stream, static_cast<QUICAppErrorCode>(0x01)));
  std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc> rst_stream_frame1 =
    QUICFrameFactory::create_rst_stream_frame(std::move(error));
  CHECK(rst_stream_frame1->error_code() == 0x01);
  CHECK(rst_stream_frame1->stream_id() == 0x1234);
  CHECK(rst_stream_frame1->final_offset() == 0);
}
