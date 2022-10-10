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

namespace proxy_wasm::common {

template <typename T, void (*deleter)(T *)>
class CSmartPtr : public std::unique_ptr<T, void (*)(T *)> {
public:
  CSmartPtr() : std::unique_ptr<T, void (*)(T *)>(nullptr, deleter) {}
  CSmartPtr(T *object) : std::unique_ptr<T, void (*)(T *)>(object, deleter) {}
};

template <typename T, void (*initializer)(T *), void (*deleter)(T *)> class CSmartType {
public:
  CSmartType() { initializer(&item); }
  ~CSmartType() { deleter(&item); }
  T *get() { return &item; }

private:
  T item;
};

} // namespace proxy_wasm::common
