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

const char *
Http3DebugNames::stream_type(Http3StreamType type)
{
  return Http3DebugNames::stream_type(static_cast<uint8_t>(type));
}

const char *
Http3DebugNames::stream_type(uint8_t type)
{
  switch (type) {
  case static_cast<uint8_t>(Http3StreamType::CONTROL):
    return "CONTROL";
  case static_cast<uint8_t>(Http3StreamType::QPACK_ENCODER):
    return "QPACK_ENCODER";
  case static_cast<uint8_t>(Http3StreamType::PUSH):
    return "PUSH";
  case static_cast<uint8_t>(Http3StreamType::QPACK_DECODER):
    return "QPACK_DECODER";
  default:
    return "UNKNOWN";
  }
}

const char *
Http3DebugNames::settings_id(uint16_t id)
{
  switch (id) {
  case static_cast<uint16_t>(Http3SettingsId::HEADER_TABLE_SIZE):
    return "HEADER_TABLE_SIZE";
  case static_cast<uint16_t>(Http3SettingsId::MAX_HEADER_LIST_SIZE):
    return "MAX_HEADER_LIST_SIZE";
  case static_cast<uint16_t>(Http3SettingsId::QPACK_BLOCKED_STREAMS):
    return "QPACK_BLOCKED_STREAMS";
  case static_cast<uint16_t>(Http3SettingsId::NUM_PLACEHOLDERS):
    return "NUM_PLACEHOLDERS";
  default:
    return "UNKNOWN";
  }
}

const char *
Http3DebugNames::error_code(uint16_t code)
{
  switch (code) {
  case static_cast<uint16_t>(Http3ErrorCode::NO_ERROR):
    return "NO_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::WRONG_SETTING_DIRECTION):
    return "WRONG_SETTING_DIRECTION";
  case static_cast<uint16_t>(Http3ErrorCode::PUSH_REFUSED):
    return "PUSH_REFUSED";
  case static_cast<uint16_t>(Http3ErrorCode::INTERNAL_ERROR):
    return "INTERNAL_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::PUSH_ALREADY_IN_CACHE):
    return "PUSH_ALREADY_IN_CACHE";
  case static_cast<uint16_t>(Http3ErrorCode::REQUEST_CANCELLED):
    return "REQUEST_CANCELLED";
  case static_cast<uint16_t>(Http3ErrorCode::INCOMPLETE_REQUEST):
    return "INCOMPLETE_REQUEST";
  case static_cast<uint16_t>(Http3ErrorCode::CONNECT_ERROR):
    return "CONNECT_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::EXCESSIVE_LOAD):
    return "EXCESSIVE_LOAD";
  case static_cast<uint16_t>(Http3ErrorCode::VERSION_FALLBACK):
    return "VERSION_FALLBACK";
  case static_cast<uint16_t>(Http3ErrorCode::WRONG_STREAM):
    return "WRONG_STREAM";
  case static_cast<uint16_t>(Http3ErrorCode::LIMIT_EXCEEDED):
    return "LIMIT_EXCEEDED";
  case static_cast<uint16_t>(Http3ErrorCode::DUPLICATE_PUSH):
    return "DUPLICATE_PUSH";
  case static_cast<uint16_t>(Http3ErrorCode::UNKNOWN_STREAM_TYPE):
    return "UNKNOWN_STREAM_TYPE";
  case static_cast<uint16_t>(Http3ErrorCode::WRONG_STREAM_COUNT):
    return "WRONG_STREAM_COUNT";
  case static_cast<uint16_t>(Http3ErrorCode::CLOSED_CRITICAL_STREAM):
    return "CLOSED_CRITICAL_STREAM";
  case static_cast<uint16_t>(Http3ErrorCode::WRONG_STREAM_DIRECTION):
    return "WRONG_STREAM_DIRECTION";
  case static_cast<uint16_t>(Http3ErrorCode::EARLY_RESPONSE):
    return "EARLY_RESPONSE";
  case static_cast<uint16_t>(Http3ErrorCode::MISSING_SETTINGS):
    return "MISSING_SETTINGS";
  case static_cast<uint16_t>(Http3ErrorCode::UNEXPECTED_FRAME):
    return "UNEXPECTED_FRAME";
  case static_cast<uint16_t>(Http3ErrorCode::REQUEST_REJECTED):
    return "REQUEST_REJECTED";
  case static_cast<uint16_t>(Http3ErrorCode::QPACK_DECOMPRESSION_FAILED):
    return "QPACK_DECOMPRESSION_FAILED";
  case static_cast<uint16_t>(Http3ErrorCode::QPACK_ENCODER_STREAM_ERROR):
    return "QPACK_ENCODER_STREAM_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::QPACK_DECODER_STREAM_ERROR):
    return "QPACK_DECODER_STREAM_ERROR";
  default:
    if (0x0100 <= code && code <= 0x01FF) {
      return "MALFORMED_FRAME";
    }

    return "UNKNOWN";
  }
}
