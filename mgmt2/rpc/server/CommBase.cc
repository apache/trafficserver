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
#include "CommBase.h"

namespace
{
struct CommInternalErrorCategory : std::error_category {
  const char *name() const noexcept override;
  std::string message(int ev) const override;
};

const char *
CommInternalErrorCategory::name() const noexcept
{
  return "comm_internal_error_category";
}

std::string
CommInternalErrorCategory::message(int ev) const
{
  switch (static_cast<rpc::comm::InternalError>(ev)) {
  case rpc::comm::InternalError::MAX_TRANSIENT_ERRORS_HANDLED:
    return {"We've reach the maximun attempt on transient errors."};
  case rpc::comm::InternalError::POLLIN_ERROR:
    return {"We haven't got a POLLIN flag back while waiting"};
  case rpc::comm::InternalError::PARTIAL_READ:
    return {"No more data to be read, but the buffer contains some invalid? data."};
  case rpc::comm::InternalError::FULL_BUFFER:
    return {"Buffer's full."};
  default:
    return "Internal Communication Error" + std::to_string(ev);
  }
}

// TODO: Make this available in the header if needed.
const CommInternalErrorCategory &
get_comm_internal_error_category()
{
  static CommInternalErrorCategory commInternalErrorCategory;
  return commInternalErrorCategory;
}

} // anonymous namespace

namespace rpc::comm
{
std::error_code
make_error_code(rpc::comm::InternalError e)
{
  return {static_cast<int>(e), get_comm_internal_error_category()};
}

} // namespace rpc::comm