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

#include <system_error>
#include "tscore/Errata.h"

namespace rpc::handlers::errors
{
// This enum defines the group errors to be used on the ts::Errata. This is just for general reference as
// it has no meaning for us.
enum class ID : int { Configuration = 1, Metrics, Records, Server, Storage, Generic };

template <typename EnumType>
constexpr auto
to_integral(EnumType e) -> typename std::underlying_type<EnumType>::type
{
  return static_cast<typename std::underlying_type<EnumType>::type>(e);
}

// Handy helper function to push the std::error_code into an errata.
static inline void
push_error(ID id, std::error_code const &ec, ts::Errata &errata)
{
  errata.push(to_integral(id), ec.value(), ec.message());
}
} // namespace rpc::handlers::errors