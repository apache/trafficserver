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

#include "RPCError.h"

#include <string>
#include <system_error> // TODO: remove

namespace
{ // anonymous namespace

struct RPCErrorCategory : std::error_category {
  const char *name() const noexcept override;
  std::string message(int ev) const override;
};

const char *
RPCErrorCategory::name() const noexcept
{
  return "rpc_msg";
}

std::string
RPCErrorCategory::message(int ev) const
{
  using namespace rpc::error;
  switch (static_cast<RPCErrorCode>(ev)) {
  case RPCErrorCode::INVALID_REQUEST:
    return {"Invalid Request"};
  case RPCErrorCode::METHOD_NOT_FOUND:
    return {"Method not found"};
  case RPCErrorCode::INVALID_PARAMS:
    return {"Invalid params"};
  case RPCErrorCode::INTERNAL_ERROR:
    return {"Internal error"};
  case RPCErrorCode::PARSE_ERROR:
    return {"Parse error"};
  // version
  case RPCErrorCode::InvalidVersion:
    return {"Invalid version, 2.0 only"};
  case RPCErrorCode::InvalidVersionType:
    return {"Invalid version type, should be a string"};
  case RPCErrorCode::MissingVersion:
    return {"Missing version field"};
  // method
  case RPCErrorCode::InvalidMethodType:
    return {"Invalid method type, should be a string"};
  case RPCErrorCode::MissingMethod:
    return {"Missing method field"};
  // params
  case RPCErrorCode::InvalidParamType:
    return {"Invalid params type, should be a structure"};
  // id
  case RPCErrorCode::InvalidIdType:
    return {"Invalid id type"};
  case RPCErrorCode::NullId:
    return {"Use of null as id is discouraged"};
  case RPCErrorCode::ExecutionError:
    return {"Error during execution"};
  default:
    return "Rpc error " + std::to_string(ev);
  }
}

const RPCErrorCategory rpcErrorCategory{};
} // anonymous namespace

namespace rpc::error
{
std::error_code
make_error_code(rpc::error::RPCErrorCode e)
{
  return {static_cast<int>(e), rpcErrorCategory};
}

} // namespace rpc::error