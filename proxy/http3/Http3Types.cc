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

#include "Http3Types.h"

Http3StreamType
Http3Stream::type(const uint8_t *buf)
{
  switch (*buf) {
  case static_cast<uint8_t>(Http3StreamType::CONTROL):
    return Http3StreamType::CONTROL;
  case static_cast<uint8_t>(Http3StreamType::PUSH):
    return Http3StreamType::PUSH;
  case static_cast<uint8_t>(Http3StreamType::QPACK_ENCODER):
    return Http3StreamType::QPACK_ENCODER;
  case static_cast<uint8_t>(Http3StreamType::QPACK_DECODER):
    return Http3StreamType::QPACK_DECODER;
  default:
    return Http3StreamType::UNKOWN;
  }
}
