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

#include "RpcError.h"

#include <string>
#include <system_error> // TODO: remove

namespace
{ // anonymous namespace

struct RpcErrorCategory : std::error_category {
  const char *name() const noexcept override;
  std::string message(int ev) const override;
};

const char *
RpcErrorCategory::name() const noexcept
{
  return "rpc_msg";
}

std::string
RpcErrorCategory::message(int ev) const
{
  using namespace rpc::jsonrpc::error;
  switch (static_cast<RpcErrorCode>(ev)) {
  case RpcErrorCode::INVALID_REQUEST:
    return {"Invalid Request"};
  case RpcErrorCode::METHOD_NOT_FOUND:
    return {"Method not found"};
  case RpcErrorCode::INVALID_PARAMS:
    return {"Invalid params"};
  case RpcErrorCode::INTERNAL_ERROR:
    return {"Internal error"};
  case RpcErrorCode::PARSE_ERROR:
    return {"Parse error"};
  // version
  case RpcErrorCode::InvalidVersion:
    return {"Invalid version, 2.0 only"};
  case RpcErrorCode::InvalidVersionType:
    return {"Invalid version type, should be a string"};
  case RpcErrorCode::MissingVersion:
    return {"Missing version field"};
  // method
  case RpcErrorCode::InvalidMethodType:
    return {"Invalid method type, should be a string"};
  case RpcErrorCode::MissingMethod:
    return {"Missing method field"};
  // params
  case RpcErrorCode::InvalidParamType:
    return {"Invalid params type, should be a structure"};
  // id
  case RpcErrorCode::InvalidIdType:
    return {"Invalid id type"};
  case RpcErrorCode::NullId:
    return {"Use of null as id is discouraged"};
  case RpcErrorCode::ExecutionError:
    return {"Error during execution"};
  default:
    return "Rpc error " + std::to_string(ev);
  }
}

const RpcErrorCategory &
get_rpc_error_category()
{
  static RpcErrorCategory rpcErrorCategory;
  return rpcErrorCategory;
}

} // anonymous namespace

namespace rpc::jsonrpc::error
{
std::error_code
make_error_code(rpc::jsonrpc::error::RpcErrorCode e)
{
  return {static_cast<int>(e), get_rpc_error_category()};
}

} // namespace rpc::jsonrpc::error