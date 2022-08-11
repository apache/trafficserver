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

#include "include/proxy-wasm/wasm_vm.h"

namespace proxy_wasm {

// A wrapper for the natively compiled NullVm plugin which implements the Wasm ABI.
class NullVmPlugin {
public:
  NullVmPlugin() = default;
  virtual ~NullVmPlugin() = default;

  // NB: These are defined rather than declared PURE because gmock uses __LINE__ internally for
  // uniqueness, making it impossible to use FOR_ALL_WASM_VM_EXPORTS with MOCK_METHOD.
#define _DEFINE_GET_FUNCTION(_T)                                                                   \
  virtual void getFunction(std::string_view, _T *f) { *f = nullptr; }
  FOR_ALL_WASM_VM_EXPORTS(_DEFINE_GET_FUNCTION)
#undef _DEFIN_GET_FUNCTIONE

  WasmVm *wasm_vm_ = nullptr;
};

using NullVmPluginFactory = std::function<std::unique_ptr<NullVmPlugin>()>;

struct RegisterNullVmPluginFactory {
  RegisterNullVmPluginFactory(std::string_view name, NullVmPluginFactory factory);
};

} // namespace proxy_wasm
