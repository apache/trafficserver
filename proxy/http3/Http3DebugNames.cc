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

#include "Http3DebugNames.h"
#include "Http3Types.h"

const char *
Http3DebugNames::frame_type(Http3FrameType type)
{
  switch (type) {
  case Http3FrameType::DATA:
    return "DATA";
  case Http3FrameType::HEADERS:
    return "HEADERS";
  case Http3FrameType::PRIORITY:
    return "PRIORITY";
  case Http3FrameType::CANCEL_PUSH:
    return "CANCEL_PUSH";
  case Http3FrameType::SETTINGS:
    return "SETTINGS";
  case Http3FrameType::PUSH_PROMISE:
    return "PUSH_PROMISE";
  case Http3FrameType::GOAWAY:
    return "GOAWAY";
  case Http3FrameType::DUPLICATE_PUSH_ID:
    return "DUPLICATE_PUSH_ID";
  case Http3FrameType::UNKNOWN:
  default:
    return "UNKNOWN";
  }
}
