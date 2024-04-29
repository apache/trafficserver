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

#include <tsutil/ts_errata.h>

#include "mgmt/rpc/config/JsonRPCConfig.h"

namespace rpc::comm
{
struct BaseCommInterface {
  virtual ~BaseCommInterface() {}
  virtual bool               configure(YAML::Node const &params) = 0;
  virtual void               run()                               = 0;
  virtual std::error_code    init()                              = 0;
  virtual bool               stop()                              = 0;
  virtual std::string const &name() const                        = 0;
};

enum class InternalError { MAX_TRANSIENT_ERRORS_HANDLED = 1, POLLIN_ERROR, PARTIAL_READ, FULL_BUFFER };
std::error_code make_error_code(rpc::comm::InternalError e);

} // namespace rpc::comm
namespace std
{
template <> struct is_error_code_enum<rpc::comm::InternalError> : true_type {
};
} // namespace std
