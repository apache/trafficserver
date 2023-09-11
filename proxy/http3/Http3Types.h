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

#pragma once

#include "tscore/ink_platform.h"

#include <memory>

enum class Http3StreamType : uint8_t {
  CONTROL       = 0x00, ///< HTTP/3
  PUSH          = 0x01, ///< HTTP/3
  QPACK_ENCODER = 0x02, ///< QPACK : encoder -> decoder
  QPACK_DECODER = 0x03, ///< QPACK : decoder -> encoder
  RESERVED      = 0x21,
  UNKNOWN       = 0xFF,
};

enum class Http3SettingsId : uint64_t {
  HEADER_TABLE_SIZE      = 0x01, ///< QPACK Settings
  RESERVED_1             = 0x02, ///< HTTP/3 Settings
  RESERVED_2             = 0x03, ///< HTTP/3 Settings
  RESERVED_3             = 0x04, ///< HTTP/3 Settings
  RESERVED_4             = 0x05, ///< HTTP/3 Settings
  MAX_FIELD_SECTION_SIZE = 0x06, ///< HTTP/3 Settings
  QPACK_BLOCKED_STREAMS  = 0x07, ///< QPACK Settings
  NUM_PLACEHOLDERS       = 0x09, ///< HTTP/3 Settings
  UNKNOWN                = 0x0A0A,
};

// Update Http3Frame::type(const uint8_t *) too when you modify this list
enum class Http3FrameType : uint64_t {
  DATA              = 0x00,
  HEADERS           = 0x01,
  PRIORITY          = 0x02,
  CANCEL_PUSH       = 0x03,
  SETTINGS          = 0x04,
  PUSH_PROMISE      = 0x05,
  X_RESERVED_1      = 0x06,
  GOAWAY            = 0x07,
  X_RESERVED_2      = 0x08,
  X_RESERVED_3      = 0x09,
  MAX_PUSH_ID       = 0x0D,
  DUPLICATE_PUSH_ID = 0x0E,
  X_MAX_DEFINED     = 0x0E,
  UNKNOWN           = 0xFF,
};

enum class Http3ErrorClass {
  UNDEFINED,
  CONNECTION,
  STREAM,
};

// Actual error code of QPACK is not decided yet on qpack-05. It will be changed.
enum class Http3ErrorCode : uint16_t {
  H3_NO_ERROR                = 0x0100,
  H3_GENERAL_PROTOCOL_ERROR  = 0x0101,
  H3_INTERNAL_ERROR          = 0x0102,
  H3_STREAM_CREATION_ERROR   = 0x0103,
  H3_CLOSED_CRITICAL_STREAM  = 0x0104,
  H3_FRAME_UNEXPECTED        = 0x0105,
  H3_FRAME_ERROR             = 0x0106,
  H3_EXCESSIVE_LOAD          = 0x0107,
  H3_ID_ERROR                = 0x0108,
  H3_SETTINGS_ERROR          = 0x0109,
  H3_MISSING_SETTINGS        = 0x010a,
  H3_REQUEST_REJECTED        = 0x010b,
  H3_REQUEST_CANCELLED       = 0x010c,
  H3_REQUEST_INCOMPLETE      = 0x010d,
  H3_MESSAGE_ERROR           = 0x010e,
  H3_CONNECT_ERROR           = 0x010f,
  H3_VERSION_FALLBACK        = 0x0110,
  QPACK_DECOMPRESSION_FAILED = 0x200,
  QPACK_ENCODER_STREAM_ERROR = 0x201,
  QPACK_DECODER_STREAM_ERROR = 0x202,
};

class Http3Error
{
public:
  virtual ~Http3Error() {}

  Http3ErrorClass cls = Http3ErrorClass::UNDEFINED;
  Http3ErrorCode code = Http3ErrorCode::H3_NO_ERROR;
  const char *msg     = nullptr;

  uint16_t
  get_code()
  {
    return static_cast<uint16_t>(code);
  }

  Http3Error() : code(Http3ErrorCode::H3_NO_ERROR){};
  Http3Error(const Http3ErrorClass error_cls, const Http3ErrorCode error_code, const char *error_msg = nullptr)
    : cls(error_cls), code(error_code), msg(error_msg){};
};

class Http3Stream
{
public:
  static Http3StreamType type(const uint8_t *buf);
};

using Http3ErrorUPtr = std::unique_ptr<Http3Error>;
