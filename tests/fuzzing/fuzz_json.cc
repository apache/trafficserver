/** @file

  fuzzing mgmt/rpc/jsonrpc

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

#include "mgmt/rpc/jsonrpc/JsonRPCManager.h"
#include "mgmt/rpc/jsonrpc/JsonRPC.h"
#include "mgmt/rpc/handlers/common/ErrorUtils.h"
#include "tscore/Diags.h"

#define kMinInputLength 5
#define kMaxInputLength 1024

// Not using the singleton logic.
struct JsonRpcUnitTest : rpc::JsonRPCManager {
  JsonRpcUnitTest() : JsonRPCManager() {}
  using base = JsonRPCManager;
  bool
  remove_handler(std::string const &name)
  {
    return base::remove_handler(name);
  }
  template <typename Func>
  bool
  add_notification_handler(const std::string &name, Func &&call)
  {
    return base::add_notification_handler(name, std::forward<Func>(call), nullptr, {});
  }
  template <typename Func>
  bool
  add_method_handler(const std::string &name, Func &&call)
  {
    return base::add_method_handler(name, std::forward<Func>(call), nullptr, {});
  }

  std::optional<std::string>
  handle_call(std::string const &jsonString)
  {
    return base::handle_call(rpc::Context{}, jsonString);
  }
};

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 1;
  }

  std::string input(reinterpret_cast<const char *>(input_data), size_data);

  DiagsPtr::set(new Diags("fuzzing", "", "", nullptr));

  JsonRpcUnitTest rpc;
  rpc.handle_call(input);

  delete diags();

  return 0;
}
