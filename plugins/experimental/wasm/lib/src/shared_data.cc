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

#include "src/shared_data.h"

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "include/proxy-wasm/vm_id_handle.h"

namespace proxy_wasm {

SharedData &getGlobalSharedData() {
  static auto *ptr = new SharedData;
  return *ptr;
};

SharedData::SharedData(bool register_vm_id_callback) {
  if (register_vm_id_callback) {
    registerVmIdHandleCallback([this](std::string_view vm_id) { this->deleteByVmId(vm_id); });
  }
}

void SharedData::deleteByVmId(std::string_view vm_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.erase(std::string(vm_id));
}

WasmResult SharedData::get(std::string_view vm_id, const std::string_view key,
                           std::pair<std::string, uint32_t> *result) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto map = data_.find(std::string(vm_id));
  if (map == data_.end()) {
    return WasmResult::NotFound;
  }
  auto it = map->second.find(std::string(key));
  if (it != map->second.end()) {
    *result = it->second;
    return WasmResult::Ok;
  }
  return WasmResult::NotFound;
}

WasmResult SharedData::keys(std::string_view vm_id, std::vector<std::string> *result) {
  result->clear();

  std::lock_guard<std::mutex> lock(mutex_);
  auto map = data_.find(std::string(vm_id));
  if (map == data_.end()) {
    return WasmResult::Ok;
  }

  for (const auto &kv : map->second) {
    result->push_back(kv.first);
  }

  return WasmResult::Ok;
}

WasmResult SharedData::set(std::string_view vm_id, std::string_view key, std::string_view value,
                           uint32_t cas) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::unordered_map<std::string, std::pair<std::string, uint32_t>> *map;
  auto map_it = data_.find(std::string(vm_id));
  if (map_it == data_.end()) {
    map = &data_[std::string(vm_id)];
  } else {
    map = &map_it->second;
  }
  auto it = map->find(std::string(key));
  if (it != map->end()) {
    if (cas != 0U && cas != it->second.second) {
      return WasmResult::CasMismatch;
    }
    it->second = std::make_pair(std::string(value), nextCas());
  } else {
    map->emplace(key, std::make_pair(std::string(value), nextCas()));
  }
  return WasmResult::Ok;
}

WasmResult SharedData::remove(std::string_view vm_id, std::string_view key, uint32_t cas,
                              std::pair<std::string, uint32_t> *result) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::unordered_map<std::string, std::pair<std::string, uint32_t>> *map;
  auto map_it = data_.find(std::string(vm_id));
  if (map_it == data_.end()) {
    return WasmResult::NotFound;
  }
  map = &map_it->second;
  auto it = map->find(std::string(key));
  if (it != map->end()) {
    if (cas != 0U && cas != it->second.second) {
      return WasmResult::CasMismatch;
    }
    if (result != nullptr) {
      *result = it->second;
    }
    map->erase(it);
    return WasmResult::Ok;
  }
  return WasmResult::NotFound;
}

} // namespace proxy_wasm
