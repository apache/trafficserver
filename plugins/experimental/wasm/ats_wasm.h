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

namespace ats_wasm
{
using proxy_wasm::ContextBase;
using proxy_wasm::PluginBase;

using proxy_wasm::AllowedCapabilitiesMap;
using proxy_wasm::WasmBase;
using proxy_wasm::WasmHandleBase;
using proxy_wasm::WasmVm;
using proxy_wasm::WasmVmFactory;
using proxy_wasm::WasmVmIntegration;

class Context;

class ATSWasmVmIntegration : public WasmVmIntegration
{
public:
  //  proxy_wasm::WasmVmIntegration
  WasmVmIntegration *
  clone() override
  {
    return new ATSWasmVmIntegration();
  }
  bool getNullVmFunction(std::string_view function_name, bool returns_word, int number_of_arguments, proxy_wasm::NullPlugin *plugin,
                         void *ptr_to_function_return) override;
  proxy_wasm::LogLevel getLogLevel() override;
  void                 error(std::string_view message) override;
  void                 trace(std::string_view message) override;
};

class Wasm : public WasmBase
{
public:
  // new constructors
  Wasm(std::unique_ptr<WasmVm> wasm_vm, std::string_view vm_id, std::string_view vm_configuration, std::string_view vm_key,
       std::unordered_map<std::string, std::string> envs, AllowedCapabilitiesMap allowed_capabilities);
  Wasm(const std::shared_ptr<WasmHandleBase> &base_wasm_handle, const WasmVmFactory &factory);

  // start a new VM
  Context *start(const std::shared_ptr<PluginBase> &plugin, TSCont cont);

  // provide access to VM mutex
  TSMutex mutex() const;

  // functions to manage lifecycle of VM
  bool readyShutdown();
  bool readyDelete();

  // functions for creating contexts from the VM
  ContextBase *createVmContext() override;
  ContextBase *createRootContext(const std::shared_ptr<PluginBase> &plugin) override;
  ContextBase *createContext(const std::shared_ptr<PluginBase> &plugin) override;

  // functions managing timer
  bool                      existsTimerPeriod(uint32_t root_context_id);
  std::chrono::milliseconds getTimerPeriod(uint32_t root_context_id);
  void                      removeTimerPeriod(uint32_t root_context_id);

  // override function for reporting error
  void error(std::string_view message) override;

protected:
  friend class Context;

private:
  TSMutex mutex_{nullptr};
};

} // namespace ats_wasm
