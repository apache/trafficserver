
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

#include "shared/rpc/RPCRequests.h"
#include "shared/rpc/yaml_codecs.h"
#include "tsutil/ts_errata.h"

constexpr int CTRL_EX_OK = 0;
// EXIT_FAILURE can also be used.
constexpr int CTRL_EX_ERROR         = 2;
constexpr int CTRL_EX_UNIMPLEMENTED = 3;
constexpr int CTRL_EX_TEMPFAIL = 75; ///< Temporary failure — operation in progress, retry later (EX_TEMPFAIL from sysexits.h).

extern int                         App_Exit_Status_Code; //!< Global variable to store the exit status code of the application.
extern swoc::Errata::severity_type App_Exit_Level_Error; //!< Minimum severity to treat as error for exit status.

inline int
appExitCodeFromResponse(const shared::rpc::JSONRPCResponse &response)
{
  if (!response.is_error()) {
    return CTRL_EX_OK;
  }

  auto err = response.error.as<shared::rpc::JSONRPCError>();
  if (err.data.empty()) {
    return CTRL_EX_ERROR;
  }

  auto effective_severity = [](auto const &e) { return swoc::Errata::severity_type(e.severity); };

  auto it = std::max_element(err.data.begin(), err.data.end(),
                             [&](auto const &a, auto const &b) { return effective_severity(a) < effective_severity(b); });

  return effective_severity(*it) >= App_Exit_Level_Error ? CTRL_EX_ERROR : CTRL_EX_OK;
}
