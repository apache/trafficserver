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

#include <mutex>

#include "include/proxy-wasm/wasm.h"

namespace proxy_wasm {

class SharedData {
public:
  SharedData(bool register_vm_id_callback = true);
  WasmResult get(std::string_view vm_id, std::string_view key,
                 std::pair<std::string, uint32_t> *result);
  WasmResult keys(std::string_view vm_id, std::vector<std::string> *result);
  WasmResult set(std::string_view vm_id, std::string_view key, std::string_view value,
                 uint32_t cas);
  WasmResult remove(std::string_view vm_id, std::string_view key, uint32_t cas,
                    std::pair<std::string, uint32_t> *result);
  void deleteByVmId(std::string_view vm_id);

private:
  uint32_t nextCas() {
    auto result = cas_;
    cas_++;
    if (cas_ == 0U) { // 0 is not a valid CAS value.
      cas_++;
    }
    return result;
  }

  // TODO: use std::shared_mutex in C++17.
  std::mutex mutex_;
  uint32_t cas_ = 1;
  std::map<std::string, std::unordered_map<std::string, std::pair<std::string, uint32_t>>> data_;
};

SharedData &getGlobalSharedData();

} // namespace proxy_wasm
