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

#include "rpc/handlers/common/convert.h"

#include "records/I_RecCore.h"
#include "records/P_RecCore.h"
#include "tscore/Diags.h"
#include "tscore/Errata.h"

#include <yaml-cpp/yaml.h>

namespace rpc::handlers::errors
{
enum class RecordError {
  RECORD_NOT_FOUND = 100,
  RECORD_NOT_CONFIG,
  RECORD_NOT_METRIC,
  INVALID_RECORD_NAME,
  VALIDITY_CHECK_ERROR,
  GENERAL_ERROR,
  RECORD_WRITE_ERROR

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
using ValidateRecType = std::function<bool(RecT, std::error_code &)>;

std::tuple<YAML::Node, std::error_code> get_yaml_record(std::string_view name, ValidateRecType check);
std::tuple<YAML::Node, std::error_code> get_config_yaml_record(std::string_view name);
std::tuple<YAML::Node, std::error_code> get_yaml_record_regex(std::string_view name, unsigned recType);

// Basic functions to helps setting a configuration record value properly. All this functions were originally in the WebMgmtUtils.
bool recordValidityCheck(std::string_view value, RecCheckT checkType, std::string_view pattern);
} // namespace rpc::handlers::records::utils