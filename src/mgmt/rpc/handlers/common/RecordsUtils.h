/* @file
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

#include <tuple>

#include "tsutil/ts_errata.h"

#include "mgmt/rpc/handlers/common/ErrorUtils.h"

#include "records/RecCore.h"
#include "tscore/Diags.h"

#include <yaml-cpp/yaml.h>

namespace rpc::handlers::errors
{
enum class RecordError {
  RECORD_NOT_FOUND = Codes::RECORD,
  RECORD_NOT_CONFIG,
  RECORD_NOT_METRIC,
  INVALID_RECORD_NAME,
  VALIDITY_CHECK_ERROR,
  GENERAL_ERROR,
  RECORD_WRITE_ERROR,
  REQUESTED_TYPE_MISMATCH,
  INVALID_INCOMING_DATA
};
std::error_code make_error_code(rpc::handlers::errors::RecordError e);
} // namespace rpc::handlers::errors

namespace std
{
template <> struct is_error_code_enum<rpc::handlers::errors::RecordError> : true_type {
};

} // namespace std

namespace rpc::handlers::records::utils
{
// response request constants
inline const std::string RECORD_NAME_REGEX_KEY{"record_name_regex"};
inline const std::string RECORD_NAME_KEY{"record_name"};
inline const std::string RECORD_VALUE_KEY{"record_value"};
inline const std::string RECORD_TYPES_KEY{"rec_types"};
inline const std::string RECORD_UPDATE_TYPE_KEY{"update_type"};
inline const std::string ERROR_CODE_KEY{"code"};
inline const std::string ERROR_MESSAGE_KEY{"message"};

using ValidateRecType = std::function<bool(RecT, std::error_code &)>;

///
/// @brief Get a Record as a YAML node
///
/// @param name The record name that is being requested.
/// @param check A function @see ValidateRecType that will be used to validate that the record we want meets the expected
/// criteria. ie: record type. Check @c RecLookupRecord API to see how it's called.
/// @return std::tuple<YAML::Node, std::error_code>
///
std::tuple<YAML::Node, std::error_code> get_yaml_record(std::string const &name, ValidateRecType check);

///
/// @brief Get a Record as a YAML node using regex as name.
///
/// @param regex The regex that will be used to lookup records.
/// @param recType The record type we want to match against the retrieved records. This could be either a single value or a bitwise
/// value.
/// @return std::tuple<YAML::Node, std::error_code>
///
std::tuple<YAML::Node, std::error_code> get_yaml_record_regex(std::string const &regex, unsigned recType);

} // namespace rpc::handlers::records::utils
