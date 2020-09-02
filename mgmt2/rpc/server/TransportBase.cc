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
#include "TransportBase.h"

namespace
{
struct TransportInternalErrorCategory : std::error_category {
  const char *name() const noexcept override;
  std::string message(int ev) const override;
};

const char *
TransportInternalErrorCategory::name() const noexcept
{
  return "transport_internal_error_category";
}

std::string
TransportInternalErrorCategory::message(int ev) const
{
  switch (static_cast<rpc::transport::InternalError>(ev)) {
  case rpc::transport::InternalError::MAX_TRANSIENT_ERRORS_HANDLED:
    return {"We've reach the maximun attempt on transient errors."};
  case rpc::transport::InternalError::POLLIN_ERROR:
    return {"We haven't got a POLLIN flag back while waiting"};
  case rpc::transport::InternalError::PARTIAL_READ:
    return {"No more data to be read, but the buffer contains some invalid? data."};
  case rpc::transport::InternalError::FULL_BUFFER:
    return {"Buffer's full."};
  default:
    return "Internal Transport impl error" + std::to_string(ev);
  }
}

const TransportInternalErrorCategory &
get_transport_internal_error_category()
{
  static TransportInternalErrorCategory transportInternalErrorCategory;
  return transportInternalErrorCategory;
}

} // anonymous namespace

namespace rpc::transport
{
std::error_code
make_error_code(rpc::transport::InternalError e)
{
  return {static_cast<int>(e), get_transport_internal_error_category()};
}

} // namespace rpc::transport