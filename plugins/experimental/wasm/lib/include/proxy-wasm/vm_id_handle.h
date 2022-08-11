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

#include <functional>
#include <mutex>
#include <memory>
#include <unordered_map>

namespace proxy_wasm {

class VmIdHandle {
public:
  VmIdHandle(std::string_view vm_id) : vm_id_(std::string(vm_id)){};
  ~VmIdHandle();

private:
  std::string vm_id_;
};

std::shared_ptr<VmIdHandle> getVmIdHandle(std::string_view vm_id);
void registerVmIdHandleCallback(const std::function<void(std::string_view vm_id)> &f);

} // namespace proxy_wasm
