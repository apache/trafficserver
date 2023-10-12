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

#include "proxy/http3/Http3DebugNames.h"
#include "proxy/http3/Http3Types.h"

const char *
Http3DebugNames::frame_type(Http3FrameType type)
{
  switch (type) {
  case Http3FrameType::DATA:
    return "DATA";
  case Http3FrameType::HEADERS:
    return "HEADERS";
  case Http3FrameType::X_RESERVED_1:
    return "X_RESERVED_1";
  case Http3FrameType::CANCEL_PUSH:
    return "CANCEL_PUSH";
  case Http3FrameType::SETTINGS:
    return "SETTINGS";
  case Http3FrameType::PUSH_PROMISE:
    return "PUSH_PROMISE";
  case Http3FrameType::X_RESERVED_2:
    return "X_RESERVED_2";
  case Http3FrameType::GOAWAY:
    return "GOAWAY";
  case Http3FrameType::X_RESERVED_3:
    return "X_RESERVED_3";
  case Http3FrameType::X_RESERVED_4:
    return "X_RESERVED_4";
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
  case static_cast<uint16_t>(Http3SettingsId::MAX_FIELD_SECTION_SIZE):
    return "MAX_FIELD_SECTION_SIZE";
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
  case static_cast<uint16_t>(Http3ErrorCode::H3_NO_ERROR):
    return "H3_NO_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_GENERAL_PROTOCOL_ERROR):
    return "H3_GENERAL_PROTOCOL_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_INTERNAL_ERROR):
    return "H3_INTERNAL_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_STREAM_CREATION_ERROR):
    return "H3_STREAM_CREATION_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_CLOSED_CRITICAL_STREAM):
    return "H3_CLOSED_CRITICAL_STREAM";
  case static_cast<uint16_t>(Http3ErrorCode::H3_FRAME_UNEXPECTED):
    return "H3_FRAME_UNEXPECTED";
  case static_cast<uint16_t>(Http3ErrorCode::H3_FRAME_ERROR):
    return "H3_FRAME_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_EXCESSIVE_LOAD):
    return "H3_EXCESSIVE_LOAD";
  case static_cast<uint16_t>(Http3ErrorCode::H3_ID_ERROR):
    return "H3_ID_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_SETTINGS_ERROR):
    return "H3_SETTINGS_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_MISSING_SETTINGS):
    return "H3_MISSING_SETTINGS";
  case static_cast<uint16_t>(Http3ErrorCode::H3_REQUEST_REJECTED):
    return "H3_REQUEST_REJECTED";
  case static_cast<uint16_t>(Http3ErrorCode::H3_REQUEST_CANCELLED):
    return "H3_REQUEST_CANCELLED";
  case static_cast<uint16_t>(Http3ErrorCode::H3_REQUEST_INCOMPLETE):
    return "H3_REQUEST_INCOMPLETE";
  case static_cast<uint16_t>(Http3ErrorCode::H3_MESSAGE_ERROR):
    return "H3_MESSAGE_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_CONNECT_ERROR):
    return "H3_CONNECT_ERROR";
  case static_cast<uint16_t>(Http3ErrorCode::H3_VERSION_FALLBACK):
    return "H3_VERSION_FALLBACK";
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
