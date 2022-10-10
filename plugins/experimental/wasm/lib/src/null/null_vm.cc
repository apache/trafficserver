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

#include "include/proxy-wasm/null_vm.h"

#include <cstring>

#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "include/proxy-wasm/null_vm_plugin.h"

namespace proxy_wasm {

std::unordered_map<std::string, NullVmPluginFactory> *null_vm_plugin_factories_ = nullptr;

RegisterNullVmPluginFactory::RegisterNullVmPluginFactory(std::string_view name,
                                                         NullVmPluginFactory factory) {
  if (null_vm_plugin_factories_ == nullptr) {
    null_vm_plugin_factories_ =
        new std::remove_reference<decltype(*null_vm_plugin_factories_)>::type;
  }
  (*null_vm_plugin_factories_)[std::string(name)] = std::move(factory);
}

std::unique_ptr<WasmVm> NullVm::clone() {
  auto cloned_null_vm = std::make_unique<NullVm>(*this);
  if (integration()) {
    cloned_null_vm->integration().reset(integration()->clone());
  }
  cloned_null_vm->load(plugin_name_, {} /* unused */, {} /* unused */);
  return cloned_null_vm;
}

// "Load" the plugin by obtaining a pointer to it from the factory.
bool NullVm::load(std::string_view plugin_name, std::string_view /*precompiled*/,
                  const std::unordered_map<uint32_t, std::string> & /*function_names*/) {
  if (null_vm_plugin_factories_ == nullptr) {
    return false;
  }
  auto factory = (*null_vm_plugin_factories_)[std::string(plugin_name)];
  if (!factory) {
    return false;
  }
  plugin_name_ = plugin_name;
  plugin_ = factory();
  plugin_->wasm_vm_ = this;
  return true;
} // namespace proxy_wasm

bool NullVm::link(std::string_view /* name */) { return true; }

uint64_t NullVm::getMemorySize() { return std::numeric_limits<uint64_t>::max(); }

// NulVm pointers are just native pointers.
std::optional<std::string_view> NullVm::getMemory(uint64_t pointer, uint64_t size) {
  if (pointer == 0 && size != 0) {
    return std::nullopt;
  }
  return std::string_view(reinterpret_cast<char *>(pointer), static_cast<size_t>(size));
}

bool NullVm::setMemory(uint64_t pointer, uint64_t size, const void *data) {
  if (pointer == 0 || data == nullptr) {
    if (size != 0) {
      return false;
    }
    return true;
  }
  auto *p = reinterpret_cast<char *>(pointer);
  memcpy(p, data, size);
  return true;
}

bool NullVm::setWord(uint64_t pointer, Word data) {
  if (pointer == 0) {
    return false;
  }
  auto *p = reinterpret_cast<char *>(pointer);
  memcpy(p, &data.u64_, sizeof(data.u64_));
  return true;
}

bool NullVm::getWord(uint64_t pointer, Word *data) {
  if (pointer == 0) {
    return false;
  }
  auto *p = reinterpret_cast<char *>(pointer);
  memcpy(&data->u64_, p, sizeof(data->u64_));
  return true;
}

size_t NullVm::getWordSize() { return sizeof(uint64_t); }

std::string_view NullVm::getPrecompiledSectionName() {
  // Return nothing: there is no WASM file.
  return {};
}

} // namespace proxy_wasm
