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

#include "include/proxy-wasm/vm_id_handle.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace proxy_wasm {

std::mutex &getGlobalIdHandleMutex() {
  static auto *ptr = new std::mutex;
  return *ptr;
}

std::vector<std::function<void(std::string_view vm_id)>> &getVmIdHandlesCallbacks() {
  static auto *ptr = new std::vector<std::function<void(std::string_view vm_id)>>;
  return *ptr;
}

std::unordered_map<std::string, std::weak_ptr<VmIdHandle>> &getVmIdHandles() {
  static auto *ptr = new std::unordered_map<std::string, std::weak_ptr<VmIdHandle>>;
  return *ptr;
}

std::shared_ptr<VmIdHandle> getVmIdHandle(std::string_view vm_id) {
  std::lock_guard<std::mutex> lock(getGlobalIdHandleMutex());
  auto key = std::string(vm_id);
  auto &handles = getVmIdHandles();

  auto it = handles.find(key);
  if (it != handles.end()) {
    auto handle = it->second.lock();
    if (handle) {
      return handle;
    }
    handles.erase(key);
  }

  auto handle = std::make_shared<VmIdHandle>(key);
  handles[key] = handle;
  return handle;
};

void registerVmIdHandleCallback(const std::function<void(std::string_view vm_id)> &f) {
  std::lock_guard<std::mutex> lock(getGlobalIdHandleMutex());
  getVmIdHandlesCallbacks().push_back(f);
}

VmIdHandle::~VmIdHandle() {
  std::lock_guard<std::mutex> lock(getGlobalIdHandleMutex());
  for (const auto &f : getVmIdHandlesCallbacks()) {
    f(vm_id_);
  }
  getVmIdHandles().erase(vm_id_);
}

} // namespace proxy_wasm
