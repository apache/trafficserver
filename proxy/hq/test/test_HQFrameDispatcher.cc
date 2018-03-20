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

#include "HQFrameDispatcher.h"
#include "Mock.h"

TEST_CASE("HQFrameHandler dispatch", "[hq]")
{
  uint8_t input[] = {// 1st frame (HEADERS)
                     0x02, 0x01, 0x00, 0x01, 0x23,
                     // 2nd frame (DATA)
                     0x04, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44,
                     // 3rd frame (incomplete)
                     0xff};

  HQFrameDispatcher hqFrameDispatcher;
  HQMockFrameHandler handler;
  hqFrameDispatcher.add_handler(&handler);
  uint16_t nread = 0;

  // Initial state
  CHECK(handler.total_frame_received == 0);
  CHECK(nread == 0);

  hqFrameDispatcher.on_read_ready(input, sizeof(input), nread);
  CHECK(handler.total_frame_received == 1);
  CHECK(nread == 12);
}
