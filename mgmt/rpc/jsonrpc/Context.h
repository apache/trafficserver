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

#include <vector>
#include <functional>
#include <string_view>

#include "tscore/Errata.h"
#include "ts/apidefs.h"
#include "rpc/handlers/common/ErrorUtils.h"

namespace rpc
{
constexpr bool RESTRICTED_API{true};
constexpr bool NON_RESTRICTED_API{false};
///
/// @brief RPC call context class.
///
/// This class is used to carry information from the transport logic to the rpc invocation logic, the transport may need to block
/// some rpc handlers from being executed which at the time of finish ups reading the raw message is yet too early to know the
/// actual handler.
///
class Context
{
  using checker_cb = std::function<void(TSRPCHandlerOptions const &, ts::Errata &)>;
  /// @brief Internal class to hold the permission checker part.
  struct Auth {
    /// Checks for permissions. This function checks for every registered permission checker.
    ///
    /// @param options Registered handler options.
    /// @return ts::Errata The errata will be filled by each of the registered checkers, if there was any issue validating the
    ///                    call, then the errata reflects that.
    ts::Errata is_blocked(TSRPCHandlerOptions const &options) const;

    /// Add permission checkers.
    template <typename F>
    void
    add_checker(F &&f)
    {
      _checkers.emplace_back(std::forward<F>(f));
    }

  private:
    std::vector<checker_cb> _checkers; ///< cb collection.
  } _auth;

public:
  Auth &
  get_auth()
  {
    return _auth;
  }
  Auth const &
  get_auth() const
  {
    return _auth;
  }
};
} // namespace rpc
