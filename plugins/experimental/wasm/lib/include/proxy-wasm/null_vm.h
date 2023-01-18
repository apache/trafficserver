// Copyright 2016-2019 Envoy Project Authors
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "include/proxy-wasm/null_vm_plugin.h"
#include "include/proxy-wasm/wasm_vm.h"

namespace proxy_wasm {

// The NullVm wraps a C++ Wasm plugin which has been compiled with the Wasm API
// and linked directly into the proxy. This is useful for development
// in that it permits the debugger to set breakpoints in both the proxy and the plugin.
struct NullVm : public WasmVm {
  NullVm() : WasmVm() {}
  NullVm(const NullVm &other) : plugin_name_(other.plugin_name_) {}

  // WasmVm
  std::string_view getEngineName() override { return "null"; }
  Cloneable cloneable() override { return Cloneable::InstantiatedModule; };
  std::unique_ptr<WasmVm> clone() override;
  bool load(std::string_view plugin_name, std::string_view precompiled,
            const std::unordered_map<uint32_t, std::string> &function_names) override;
  bool link(std::string_view debug_name) override;
  uint64_t getMemorySize() override;
  std::optional<std::string_view> getMemory(uint64_t pointer, uint64_t size) override;
  bool setMemory(uint64_t pointer, uint64_t size, const void *data) override;
  bool setWord(uint64_t pointer, Word data) override;
  bool getWord(uint64_t pointer, Word *data) override;
  size_t getWordSize() override;
  std::string_view getPrecompiledSectionName() override;

#define _FORWARD_GET_FUNCTION(_T)                                                                  \
  void getFunction(std::string_view function_name, _T *f) override {                               \
    plugin_->getFunction(function_name, f);                                                        \
  }
  FOR_ALL_WASM_VM_EXPORTS(_FORWARD_GET_FUNCTION)
#undef _FORWARD_GET_FUNCTION

  // These are not needed for NullVm which invokes the handlers directly.
#define _REGISTER_CALLBACK(_T)                                                                     \
  void registerCallback(std::string_view, std::string_view, _T,                                    \
                        typename ConvertFunctionTypeWordToUint32<_T>::type) override{};
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_CALLBACK)
#undef _REGISTER_CALLBACK

  void terminate() override {}
  bool usesWasmByteOrder() override { return false; }

  std::string plugin_name_;
  std::unique_ptr<NullVmPlugin> plugin_;
};

} // namespace proxy_wasm
