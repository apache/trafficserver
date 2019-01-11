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

#include "QUICFrameRetransmitter.h"

constexpr static uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x10};

TEST_CASE("QUICFrameRetransmitter ignore frame which can not be retranmistted", "[quic]")
{
  QUICFrameRetransmitter retransmitter;
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = QUICFrameType::PING;
  info->level                   = QUICEncryptionLevel::NONE;

  retransmitter.save_frame_info(info);
  CHECK(retransmitter.create_retransmitted_frame(QUICEncryptionLevel::INITIAL, UINT16_MAX) == nullptr);
}

TEST_CASE("QUICFrameRetransmitter ignore frame which can not be split", "[quic]")
{
  QUICFrameRetransmitter retransmitter;
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = QUICFrameType::STOP_SENDING;
  info->level                   = QUICEncryptionLevel::NONE;

  retransmitter.save_frame_info(info);
  CHECK(retransmitter.create_retransmitted_frame(QUICEncryptionLevel::INITIAL, 0) == nullptr);
}

TEST_CASE("QUICFrameRetransmitter ignore frame which has wrong level", "[quic]")
{
  QUICFrameRetransmitter retransmitter;
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = QUICFrameType::STOP_SENDING;
  info->level                   = QUICEncryptionLevel::HANDSHAKE;

  retransmitter.save_frame_info(info);
  CHECK(retransmitter.create_retransmitted_frame(QUICEncryptionLevel::INITIAL, UINT16_MAX) == nullptr);
}

TEST_CASE("QUICFrameRetransmitter successfully create retransmitted frame", "[quic]")
{
  QUICFrameRetransmitter retransmitter;
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = QUICFrameType::STOP_SENDING;
  info->level                   = QUICEncryptionLevel::INITIAL;

  retransmitter.save_frame_info(info);

  auto frame = retransmitter.create_retransmitted_frame(QUICEncryptionLevel::INITIAL, UINT16_MAX);
  CHECK(frame != nullptr);
  CHECK(frame->type() == QUICFrameType::STOP_SENDING);
}

TEST_CASE("QUICFrameRetransmitter successfully create stream frame", "[quic]")
{
  QUICFrameRetransmitter retransmitter;
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = QUICFrameType::STREAM;
  info->level                   = QUICEncryptionLevel::INITIAL;

  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc();
  memcpy(block->start(), data, sizeof(data));
  block->fill(sizeof(data));

  StreamFrameInfo *frame_info = reinterpret_cast<StreamFrameInfo *>(info->data);
  frame_info->stream_id       = 0x12345;
  frame_info->offset          = 0x67890;
  frame_info->block           = block;

  CHECK(block->refcount() == 2);
  retransmitter.save_frame_info(info);
  CHECK(block->refcount() == 2); // block's refcount doesn't change

  auto frame = retransmitter.create_retransmitted_frame(QUICEncryptionLevel::INITIAL, UINT16_MAX);
  CHECK(frame != nullptr);
  CHECK(frame->type() == QUICFrameType::STREAM);
  auto stream_frame = static_cast<QUICStreamFrame *>(frame.get());
  CHECK(stream_frame->stream_id() == 0x12345);
  CHECK(stream_frame->offset() == 0x67890);
  CHECK(stream_frame->data_length() == sizeof(data));
  CHECK(memcmp(stream_frame->data(), data, sizeof(data)) == 0);
  frame = QUICFrameFactory::create_null_frame();
  // Becasue the info has been released, the refcount should be 1 (var block).
  CHECK(block->refcount() == 1);
}

TEST_CASE("QUICFrameRetransmitter successfully split stream frame", "[quic]")
{
  QUICFrameRetransmitter retransmitter;
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = QUICFrameType::STREAM;
  info->level                   = QUICEncryptionLevel::INITIAL;

  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc();
  memcpy(block->start(), data, sizeof(data));
  block->fill(sizeof(data));

  StreamFrameInfo *frame_info = reinterpret_cast<StreamFrameInfo *>(info->data);
  frame_info->stream_id       = 0x12345;
  frame_info->offset          = 0x67890;
  frame_info->block           = block;
  CHECK(block->refcount() == 2);

  retransmitter.save_frame_info(info);

  auto frame = retransmitter.create_retransmitted_frame(QUICEncryptionLevel::INITIAL, 15);
  CHECK(frame != nullptr);
  CHECK(frame->type() == QUICFrameType::STREAM);
  auto stream_frame = static_cast<QUICStreamFrame *>(frame.get());
  CHECK(stream_frame->stream_id() == 0x12345);
  CHECK(stream_frame->offset() == 0x67890);
  CHECK(stream_frame->size() <= 15);

  auto size = stream_frame->data_length();
  CHECK(memcmp(stream_frame->data(), data, stream_frame->data_length()) == 0);
  // one for var block, one for the left data which saved in retransmitter
  CHECK(block->data->refcount() == 2);
  // one for var block, one for the left data which saved in retransmitter, one for var frame
  CHECK(block->refcount() == 2);
  frame = QUICFrameFactory::create_null_frame();
  // one for var block, one for var info
  CHECK(block->refcount() == 2);
  CHECK(block->data->refcount() == 1);

  frame = retransmitter.create_retransmitted_frame(QUICEncryptionLevel::INITIAL, UINT16_MAX);
  CHECK(frame != nullptr);
  CHECK(frame->type() == QUICFrameType::STREAM);
  stream_frame = static_cast<QUICStreamFrame *>(frame.get());
  CHECK(stream_frame->stream_id() == 0x12345);
  CHECK(stream_frame->offset() == 0x67890 + size);
  CHECK(stream_frame->data_length() == sizeof(data) - size);
  CHECK(memcmp(stream_frame->data(), data + size, stream_frame->data_length()) == 0);
  CHECK(block->refcount() == 1); // one for var block

  frame = QUICFrameFactory::create_null_frame();
  CHECK(block->refcount() == 1);
  CHECK(block->data->refcount() == 1);
}
