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

#include <tscore/Errata.h>

#include "rpc/config/JsonRpcConfig.h"

namespace rpc::transport
{
struct BaseTransportInterface {
  virtual ~BaseTransportInterface() {}
  virtual ts::Errata configure(YAML::Node const &params) = 0;
  virtual void run()                                     = 0;
  virtual ts::Errata init()                              = 0;
  virtual bool stop()                                    = 0;
  virtual std::string_view name() const                  = 0;
};

enum class InternalError { MAX_TRANSIENT_ERRORS_HANDLED = 1, POLLIN_ERROR, PARTIAL_READ, FULL_BUFFER };
std::error_code make_error_code(rpc::transport::InternalError e);

} // namespace rpc::transport
namespace std
{
template <> struct is_error_code_enum<rpc::transport::InternalError> : true_type {
};
} // namespace std