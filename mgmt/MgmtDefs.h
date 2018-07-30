/** @file

  Some mgmt definitions for relatively general use.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#pragma once

/*
 * Type definitions.
 */
#include <functional>
#include <string_view>

#include "ts/ink_defs.h"

typedef int64_t MgmtIntCounter;
typedef int64_t MgmtInt;
typedef int8_t MgmtByte;
typedef float MgmtFloat;
typedef char *MgmtString;

typedef enum {
  MGMT_INVALID  = -1,
  MGMT_INT      = 0,
  MGMT_FLOAT    = 1,
  MGMT_STRING   = 2,
  MGMT_COUNTER  = 3,
  MGMT_TYPE_MAX = 4,
} MgmtType;

/*
 * MgmtCallback
 *   Management Callback functions.
 */
typedef void *(*MgmtCallback)(void *opaque_cb_data, char *data_raw, int data_len);

//-------------------------------------------------------------------------
// API conversion functions.
//-------------------------------------------------------------------------
/** Conversion functions to and from an aribrary type and Management types.
 *
 * A type that wants to support conversion in the TS API should create a static instance of this
 * class and fill in the appropriate members. The TS API set/get functions can then check for a
 * @c nullptr to see if the conversion is supported and if so, call a function to do that. The
 * @c void* argument is a raw pointer to the typed object. For instance, if this is for transaction
 * overrides the pointer will be to the member in the transaction override configuration structure.
 * Support for the management types is built in, this is only needed for types that aren't defined
 * in this header.
 */
struct MgmtConverter {
  // MgmtInt conversions.
  std::function<MgmtInt(void *)> get_int{nullptr};
  std::function<void(void *, MgmtInt)> set_int{nullptr};

  // MgmtFloat conversions.
  std::function<MgmtFloat(void *)> get_float{nullptr};
  std::function<void(void *, MgmtFloat)> set_float{nullptr};

  // MgmtString conversions.
  // This is a bit different because it takes std::string_view instead of MgmtString but that's
  // worth the difference.
  std::function<std::string_view(void *)> get_string{nullptr};
  std::function<void(void *, std::string_view)> set_string{nullptr};
};

#define LM_CONNECTION_SERVER "processerver.sock"
