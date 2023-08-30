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

#include "ats_wasm.h"

#include <string>

namespace ats_wasm
{
// extended Wasm VM Integration object
proxy_wasm::LogLevel
ATSWasmVmIntegration::getLogLevel()
{
  if (dbg_ctl.on()) {
    return proxy_wasm::LogLevel::debug;
  } else {
    return proxy_wasm::LogLevel::error;
  }
}

void
ATSWasmVmIntegration::error(std::string_view message)
{
  TSError("%.*s", static_cast<int>(message.size()), message.data());
}

void
ATSWasmVmIntegration::trace(std::string_view message)
{
  Dbg(dbg_ctl, "%.*s", static_cast<int>(message.size()), message.data());
}

bool
ATSWasmVmIntegration::getNullVmFunction(std::string_view function_name, bool returns_word, int number_of_arguments,
                                        proxy_wasm::NullPlugin *plugin, void *ptr_to_function_return)
{
  return false;
}

// extended constructors to initialize mutex
Wasm::Wasm(const std::shared_ptr<WasmHandleBase> &base_wasm_handle, const WasmVmFactory &factory)
  : WasmBase(base_wasm_handle, factory)
{
  mutex_ = TSMutexCreate();
}

Wasm::Wasm(std::unique_ptr<WasmVm> wasm_vm, std::string_view vm_id, std::string_view vm_configuration, std::string_view vm_key,
           std::unordered_map<std::string, std::string> envs, AllowedCapabilitiesMap allowed_capabilities)
  : WasmBase(std::move(wasm_vm), vm_id, vm_configuration, vm_key, std::move(envs), std::move(allowed_capabilities))
{
  mutex_ = TSMutexCreate();
}

// function to retrieve mutex
TSMutex
Wasm::mutex() const
{
  return mutex_;
}

// functions to create contexts
ContextBase *
Wasm::createVmContext()
{
  return new Context(this);
}

ContextBase *
Wasm::createRootContext(const std::shared_ptr<PluginBase> &plugin)
{
  Dbg(dbg_ctl, "Create root context for ats plugin");
  return new Context(this, plugin);
}

ContextBase *
Wasm::createContext(const std::shared_ptr<PluginBase> &plugin)
{
  return new Context(this, plugin);
}

// Function to start a new root context
Context *
Wasm::start(const std::shared_ptr<PluginBase> &plugin, TSCont contp)
{
  auto it = root_contexts_.find(plugin->key());
  if (it != root_contexts_.end()) {
    auto *c = static_cast<Context *>(it->second.get());
    if (c->scheduler_cont() == nullptr) {
      c->initialize(contp);
    } else {
      // keep the old continuation
      TSContDestroy(contp);
    }
    c->onStart(plugin);
    return c;
  }
  auto context      = std::unique_ptr<Context>(static_cast<Context *>(createRootContext(plugin)));
  auto *context_ptr = context.get();
  context_ptr->initialize(contp);
  root_contexts_[plugin->key()] = std::move(context);
  if (!context_ptr->onStart(plugin)) {
    TSContDestroy(contp);
    return nullptr;
  }
  return context_ptr;
}

// functions to manage lifecycle of VM
bool
Wasm::readyShutdown()
{
  // if there is a non-root context, there is an unfinished transaction
  for (const auto &n : contexts_) {
    if (!n.second->isRootContext()) {
      return false;
    }
  }

  // if there is an entry in timer_period_, there is a continuation still running for that root context
  return timer_period_.empty();
}

bool
Wasm::readyDelete()
{
  return (root_contexts_.empty() && pending_done_.empty() && pending_delete_.empty());
}

// functions to manage timer
bool
Wasm::existsTimerPeriod(uint32_t root_context_id)
{
  return timer_period_.find(root_context_id) != timer_period_.end();
}

std::chrono::milliseconds
Wasm::getTimerPeriod(uint32_t root_context_id)
{
  return timer_period_[root_context_id];
}

void
Wasm::removeTimerPeriod(uint32_t root_context_id)
{
  auto it = timer_period_.find(root_context_id);
  if (it != timer_period_.end()) {
    timer_period_.erase(it);
  }
}

// function to override error report
void
Wasm::error(std::string_view message)
{
  TSError("%.*s", static_cast<int>(message.size()), message.data());
}

} // namespace ats_wasm
