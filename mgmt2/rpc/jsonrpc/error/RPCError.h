/**
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

#include <string>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <cassert>
#include <optional>
#include <system_error>

namespace rpc::error
{
enum class RPCErrorCode {
  // for std::error_code to work, we shouldn't define 0.

  // JSONRPC 2.0 protocol defined errors.
  INVALID_REQUEST  = -32600,
  METHOD_NOT_FOUND = -32601,
  INVALID_PARAMS   = -32602,
  INTERNAL_ERROR   = -32603,
  PARSE_ERROR      = -32700,

  // Custom errors. A more grained error codes than the main above.
  InvalidVersion = 1, //!< Version should be equal to "2.0".
  InvalidVersionType, //!< Invalid string conversion.
  MissingVersion,     //!< Missing version field.
  InvalidMethodType,  //!< Should be a string.
  MissingMethod,      //!< Method name missing.
  InvalidParamType,   //!< Not a valid structured type.
  InvalidIdType,      //!< Invalid string conversion.
  NullId,             //!< null id.
  ExecutionError,     //!< Handler's general error.
  Unauthorized,       //!< In case we want to block the call based on privileges, access permissions, etc.
  EmptyId             //!< Empty id("").
};
// TODO: force non 0 check
std::error_code make_error_code(rpc::error::RPCErrorCode e);

} // namespace rpc::error

namespace std
{
template <> struct is_error_code_enum<rpc::error::RPCErrorCode> : true_type {
};
} // namespace std
