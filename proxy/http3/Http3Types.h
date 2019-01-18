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
  CONTROL = 0x43,
  PUSH    = 0x50,
};

// Update Http3Frame::type(const uint8_t *) too when you modify this list
enum class Http3FrameType : uint8_t {
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
  NONE,
  APPLICATION,
};

using Http3AppErrorCode = uint16_t;

class Http3Error
{
public:
  virtual ~Http3Error() {}
  uint16_t code();

  Http3ErrorClass cls = Http3ErrorClass::NONE;
  union {
    Http3AppErrorCode app_error_code;
  };
  const char *msg = nullptr;

protected:
  Http3Error(){};
  Http3Error(const Http3AppErrorCode error_code, const char *error_msg = nullptr)
    : cls(Http3ErrorClass::APPLICATION), app_error_code(error_code), msg(error_msg){};
};

class Http3NoError : public Http3Error
{
public:
  Http3NoError() : Http3Error() {}
};

class Http3ConnectionError : public Http3Error
{
public:
  Http3ConnectionError() : Http3Error() {}
  Http3ConnectionError(const Http3AppErrorCode error_code, const char *error_msg = nullptr) : Http3Error(error_code, error_msg){};
};

class Http3Stream;

class Http3StreamError : public Http3Error
{
public:
  Http3StreamError() : Http3Error() {}
  Http3StreamError(Http3Stream *s, const Http3AppErrorCode error_code, const char *error_msg = nullptr)
    : Http3Error(error_code, error_msg), stream(s){};

  Http3Stream *stream;
};

using Http3ErrorUPtr           = std::unique_ptr<Http3Error>;
using Http3ConnectionErrorUPtr = std::unique_ptr<Http3ConnectionError>;
using Http3StreamErrorUPtr     = std::unique_ptr<Http3StreamError>;
