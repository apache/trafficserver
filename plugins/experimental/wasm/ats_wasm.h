/*
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

#include "include/proxy-wasm/wasm.h"
#include "ats_context.h"

#include "ts/ts.h"

namespace proxy_wasm
{
class Context;

class Wasm : public WasmBase
{
public:
  // new constructors
  Wasm(std::unique_ptr<WasmVm> wasm_vm, std::string_view vm_id, std::string_view vm_configuration, std::string_view vm_key,
       std::unordered_map<std::string, std::string> envs, AllowedCapabilitiesMap allowed_capabilities);
  Wasm(const std::shared_ptr<WasmHandleBase> &base_wasm_handle, const WasmVmFactory &factory);

  // start a new VM
  Context *start(std::shared_ptr<PluginBase> plugin, TSCont cont);

  // provide access to VM mutex
  TSMutex mutex() const;

  // functions to manage lifecycle of VM
  bool readyShutdown();
  bool readyDelete();

  // functions for creating contexts from the VM
  ContextBase *createVmContext();
  ContextBase *createRootContext(const std::shared_ptr<PluginBase> &plugin);
  ContextBase *createContext(const std::shared_ptr<PluginBase> &plugin);

  // functions managing timer
  bool existsTimerPeriod(uint32_t root_context_id);
  std::chrono::milliseconds getTimerPeriod(uint32_t root_context_id);
  void removeTimerPeriod(uint32_t root_context_id);

  // override function for reporting error
  void error(std::string_view message);

protected:
  friend class Context;

private:
  TSMutex mutex_;
};

} // namespace proxy_wasm
