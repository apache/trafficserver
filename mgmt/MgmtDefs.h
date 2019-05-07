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

#include "tscore/ink_defs.h"
#include "tscpp/util/MemSpan.h"
#include "tscpp/util/TextView.h"

typedef int64_t MgmtIntCounter;
typedef int64_t MgmtInt;
typedef int8_t MgmtByte;
typedef float MgmtFloat;
typedef char *MgmtString;

enum MgmtType {
  MGMT_INVALID  = -1,
  MGMT_INT      = 0,
  MGMT_FLOAT    = 1,
  MGMT_STRING   = 2,
  MGMT_COUNTER  = 3,
  MGMT_TYPE_MAX = 4,
};

/// Management callback signature.
/// The memory span is the message payload for the callback.
/// This can be a lambda, which should be used if additional context information is needed.
using MgmtCallback = std::function<void(ts::MemSpan)>;

//-------------------------------------------------------------------------
// API conversion functions.
//-------------------------------------------------------------------------
/** Conversion functions to and from an arbitrary type and Management types.
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
  /** Load a native type into a @c MgmtInt
   *
   * This is passed a @c void* which is a pointer to the member in the configuration instance.
   * This function must return a @c MgmtInt converted from that value.
   */
  MgmtInt (*load_int)(void *) = nullptr;

  /** Store a @c MgmtInt into a native type.
   *
   * This function is passed a @c void* which is a pointer to the member in the configuration
   * instance and a @c MgmtInt. The member should be updated to correspond to the @c MgmtInt value.
   */
  void (*store_int)(void *, MgmtInt) = nullptr;

  /** Load a @c MgmtFloat from a native type.
   *
   * This is passed a @c void* which is a pointer to the member in the configuration instance.
   * This function must return a @c MgmtFloat converted from that value.
   */
  MgmtFloat (*load_float)(void *) = nullptr;

  /** Store a @c MgmtFloat into a native type.
   *
   * This function is passed a @c void* which is a pointer to the member in the configuration
   * instance and a @c MgmtFloat. The member should be updated to correspond to the @c MgmtFloat value.
   */
  void (*store_float)(void *, MgmtFloat) = nullptr;

  /** Load a native type into view.
   *
   * This is passed a @c void* which is a pointer to the member in the configuration instance.
   * This function must return a @c string_view which contains the text for the member.
   */
  std::string_view (*load_string)(void *) = nullptr;

  /** Store a view in a native type.
   *
   * This is passed a @c void* which is a pointer to the member in the configuration instance.
   * This function must return a @c string_view which contains the text for the member.
   */
  void (*store_string)(void *, std::string_view) = nullptr;

  // Convenience constructors because generally only one pair is valid.
  MgmtConverter(MgmtInt (*load)(void *), void (*store)(void *, MgmtInt));
  MgmtConverter(MgmtFloat (*load)(void *), void (*store)(void *, MgmtFloat));
  MgmtConverter(std::string_view (*load)(void *), void (*store)(void *, std::string_view));

  MgmtConverter(MgmtInt (*_load_int)(void *), void (*_store_int)(void *, MgmtInt), MgmtFloat (*_load_float)(void *),
                void (*_store_float)(void *, MgmtFloat), std::string_view (*_load_string)(void *),
                void (*_store_string)(void *, std::string_view));
};

inline MgmtConverter::MgmtConverter(MgmtInt (*load)(void *), void (*store)(void *, MgmtInt)) : load_int(load), store_int(store) {}

inline MgmtConverter::MgmtConverter(MgmtFloat (*load)(void *), void (*store)(void *, MgmtFloat))
  : load_float(load), store_float(store)
{
}

inline MgmtConverter::MgmtConverter(std::string_view (*load)(void *), void (*store)(void *, std::string_view))
  : load_string(load), store_string(store)
{
}

inline MgmtConverter::MgmtConverter(MgmtInt (*_load_int)(void *), void (*_store_int)(void *, MgmtInt),
                                    MgmtFloat (*_load_float)(void *), void (*_store_float)(void *, MgmtFloat),
                                    std::string_view (*_load_string)(void *), void (*_store_string)(void *, std::string_view))
  : load_int(_load_int),
    store_int(_store_int),
    load_float(_load_float),
    store_float(_store_float),
    load_string(_load_string),
    store_string(_store_string)
{
}

constexpr ts::TextView LM_CONNECTION_SERVER{"processerver.sock"};
