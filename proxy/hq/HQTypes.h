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

// Update HQFrame::type(const uint8_t *) too when you modify this list
enum class HQFrameType : uint8_t {
  DATA          = 0x00,
  HEADERS       = 0x01,
  PRIORITY      = 0x02,
  CANCEL_PUSH   = 0x03,
  SETTINGS      = 0x04,
  PUSH_PROMISE  = 0x05,
  X_RESERVED_1  = 0x06,
  GOAWAY        = 0x07,
  HEADER_ACK    = 0x08,
  X_RESERVED_2  = 0x09,
  MAX_PUSH_ID   = 0x0D,
  X_MAX_DEFINED = 0x0D,
  UNKNOWN       = 0xFF,
};

enum class HQErrorClass {
  NONE,
  APPLICATION,
};

using HQAppErrorCode = uint16_t;

class HQError
{
public:
  virtual ~HQError() {}
  uint16_t code();

  HQErrorClass cls = HQErrorClass::NONE;
  union {
    HQAppErrorCode app_error_code;
  };
  const char *msg = nullptr;

protected:
  HQError(){};
  HQError(const HQAppErrorCode error_code, const char *error_msg = nullptr)
    : cls(HQErrorClass::APPLICATION), app_error_code(error_code), msg(error_msg){};
};

class HQNoError : public HQError
{
public:
  HQNoError() : HQError() {}
};

class HQConnectionError : public HQError
{
public:
  HQConnectionError() : HQError() {}
  HQConnectionError(const HQAppErrorCode error_code, const char *error_msg = nullptr) : HQError(error_code, error_msg){};
};

class HQStream;

class HQStreamError : public HQError
{
public:
  HQStreamError() : HQError() {}
  HQStreamError(HQStream *s, const HQAppErrorCode error_code, const char *error_msg = nullptr)
    : HQError(error_code, error_msg), stream(s){};

  HQStream *stream;
};

using HQErrorUPtr           = std::unique_ptr<HQError>;
using HQConnectionErrorUPtr = std::unique_ptr<HQConnectionError>;
using HQStreamErrorUPtr     = std::unique_ptr<HQStreamError>;
