/** @file

  Readable C++ type names.

  @section license License

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

#include "tscore/Demangle.h"

#include <cstdlib>
#include <cxxabi.h>
#include <exception>
#include <memory>

namespace
{
struct FreeDeleter {
  void
  operator()(char *p) const noexcept
  {
    std::free(p);
  }
};
} // namespace

namespace ts
{
std::string
demangle(std::type_info const &type) noexcept
{
  try {
    int                                status = 0;
    std::unique_ptr<char, FreeDeleter> name{abi::__cxa_demangle(type.name(), nullptr, nullptr, &status)};

    return status == 0 && name != nullptr ? name.get() : type.name();
  } catch (...) {
    return "<unknown>";
  }
}

std::string
describe_current_exception() noexcept
{
  std::exception_ptr const ep = std::current_exception();

  try {
    if (!ep) {
      return "no active exception";
    }
    try {
      std::rethrow_exception(ep);
    } catch (std::exception const &ex) {
      return demangle(typeid(ex)) + ": " + ex.what();
    } catch (...) {
      if (std::type_info const *type = abi::__cxa_current_exception_type(); type != nullptr) {
        return "exception of type " + demangle(*type);
      }
    }
  } catch (...) {
  }
  return "exception";
}
} // namespace ts
