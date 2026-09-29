
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

#include <algorithm>
#include <exception>
#include <optional>
#include <string_view>

#include "mgmt/rpc/jsonrpc/error/RPCError.h"
#include "shared/rpc/RPCRequests.h"
#include "shared/rpc/yaml_codecs.h"
#include "swoc/string_view_util.h"
#include "tsutil/ts_errata.h"

constexpr int CTRL_EX_OK = 0;
// EXIT_FAILURE can also be used.
constexpr int CTRL_EX_ERROR         = 2;
constexpr int CTRL_EX_UNIMPLEMENTED = 3;
constexpr int CTRL_EX_TEMPFAIL = 75; ///< Temporary failure — operation in progress, retry later (EX_TEMPFAIL from sysexits.h).

extern int                         App_Exit_Status_Code; //!< Global variable to store the exit status code of the application.
extern swoc::Errata::severity_type App_Exit_Level_Error; //!< Lowest annotation severity that makes a server error exit non zero.

/// Thrown by a command step that already printed a server error and graded it into @c App_Exit_Status_Code, so the command
/// stops there and the error is not reported a second time.
struct ServerErrorReported : std::exception {
  const char *
  what() const noexcept override
  {
    return "server error already reported";
  }
};

/// Parse an @c --error-level value: a severity name (case-insensitive), or "warning" as an alias of "warn".
inline std::optional<swoc::Errata::severity_type>
parse_error_level(std::string_view name)
{
  if (strcasecmp(name, std::string_view{"warning"}) == 0) {
    return swoc::Errata::severity_type(ERRATA_WARN);
  }
  for (size_t i = 0; i < Severity_Names.size(); ++i) {
    if (strcasecmp(name, Severity_Names[i]) == 0) {
      return swoc::Errata::severity_type(i);
    }
  }
  return std::nullopt;
}

/// Severity an error data entry is graded with. A missing severity counts as an error, which is how the server classifies an
/// annotation without one. A severity that is not one of the known levels counts as the highest level, so it always fails.
inline swoc::Errata::severity_type
exit_severity_of(shared::rpc::JSONRPCError::DataEntry const &entry)
{
  if (!entry.severity) {
    return ERRATA_ERROR;
  }
  if (*entry.severity < 0 || *entry.severity > ERRATA_EMERGENCY) {
    return ERRATA_EMERGENCY;
  }
  return swoc::Errata::severity_type(*entry.severity);
}

/// Exit status for a server response. Only the annotations of a method handler failure (ExecutionError) are graded against
/// @c App_Exit_Level_Error. Any other error, or one without annotations, always exits with @c CTRL_EX_ERROR.
inline int
appExitCodeFromResponse(shared::rpc::JSONRPCResponse const &response)
{
  if (!response.is_error()) {
    return CTRL_EX_OK;
  }

  shared::rpc::JSONRPCError err;
  try {
    err = response.error.as<shared::rpc::JSONRPCError>();
  } catch (std::exception const &) {
    return CTRL_EX_ERROR;
  }

  if (err.code != static_cast<int32_t>(rpc::error::RPCErrorCode::ExecutionError) || err.data.empty()) {
    return CTRL_EX_ERROR;
  }

  auto const worst = std::ranges::max_element(err.data, {}, exit_severity_of);

  return exit_severity_of(*worst) >= App_Exit_Level_Error ? CTRL_EX_ERROR : CTRL_EX_OK;
}
