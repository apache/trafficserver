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

#include "quic/QUICFrameDispatcher.h"
#include "quic/Mock.h"
#include <memory>

TEST_CASE("QUICFrameHandler", "[quic]")
{
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(BUFFER_SIZE_INDEX_32K);
  block->fill(1);
  CHECK(block->read_avail() == 1);

  QUICStreamFrame streamFrame(block, 0x03, 0);

  MockQUICContext context;

  MockQUICConnection connection;
  MockQUICStreamManager streamManager = {&context};
  MockQUICConnectionInfoProvider info;
  MockQUICLossDetector lossDetector(context);

  QUICFrameDispatcher quicFrameDispatcher(&info);
  quicFrameDispatcher.add_handler(&connection);
  quicFrameDispatcher.add_handler(&streamManager);
  quicFrameDispatcher.add_handler(&lossDetector);

  // Initial state
  CHECK(connection.getTotalFrameCount() == 0);
  CHECK(streamManager.getTotalFrameCount() == 0);

  // STREAM frame
  uint8_t buf[4096]      = {0};
  size_t len             = 0;
  Ptr<IOBufferBlock> ibb = streamFrame.to_io_buffer_block(sizeof(buf));
  for (auto b = ibb; b; b = b->next) {
    memcpy(buf + len, b->start(), b->size());
    len += b->size();
  }
  bool should_send_ack;
  bool is_flow_controlled;
  quicFrameDispatcher.receive_frames(context, QUICEncryptionLevel::INITIAL, buf, len, should_send_ack, is_flow_controlled, nullptr,
                                     nullptr);
  CHECK(connection.getTotalFrameCount() == 0);
  CHECK(streamManager.getTotalFrameCount() == 1);

  // CONNECTION_CLOSE frame
  QUICConnectionCloseFrame connectionCloseFrame(0, 0, "", 0, nullptr);
  ibb = connectionCloseFrame.to_io_buffer_block(sizeof(buf));
  len = 0;
  for (auto b = ibb; b; b = b->next) {
    memcpy(buf + len, b->start(), b->size());
    len += b->size();
  }
  quicFrameDispatcher.receive_frames(context, QUICEncryptionLevel::INITIAL, buf, len, should_send_ack, is_flow_controlled, nullptr,
                                     nullptr);
  CHECK(connection.getTotalFrameCount() == 1);
  CHECK(streamManager.getTotalFrameCount() == 1);
}
