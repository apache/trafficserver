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
#include "mgmt/rpc/handlers/common/ErrorUtils.h"

#include <system_error>
#include <string>

namespace
{ // anonymous namespace

struct RPCHandlerLogicErrorCategory : std::error_category {
  const char *name() const noexcept override;
  std::string message(int ev) const override;
};

const char *
RPCHandlerLogicErrorCategory::name() const noexcept
{
  return "rpc_handler_logic_error";
}
std::string
RPCHandlerLogicErrorCategory::message(int ev) const
{
  switch (static_cast<rpc::handlers::errors::Codes>(ev)) {
  case rpc::handlers::errors::Codes::CONFIGURATION:
    return {"Configuration handling error."};
  case rpc::handlers::errors::Codes::METRIC:
    return {"Metric handling error."};
  case rpc::handlers::errors::Codes::RECORD:
    return {"Record handling error."};
  case rpc::handlers::errors::Codes::SERVER:
    return {"Server handling error."};
  case rpc::handlers::errors::Codes::STORAGE:
    return {"Storage handling error."};
  case rpc::handlers::errors::Codes::PLUGIN:
    return {"Plugin handling error."};
  default:
    return "Generic handling error: " + std::to_string(ev);
  }
}

const RPCHandlerLogicErrorCategory rpcHandlerLogicErrorCategory{};
} // anonymous namespace

namespace rpc::handlers::errors
{
std::error_code
make_error_code(rpc::handlers::errors::Codes e)
{
  return {static_cast<int>(e), rpcHandlerLogicErrorCategory};
}
} // namespace rpc::handlers::errors
