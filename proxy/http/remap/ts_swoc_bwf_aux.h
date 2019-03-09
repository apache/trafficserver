/** @file

  Auxillary / local extensions to libswoc BufferWriter formatting.

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

#pragma once

#include "swoc/bwf_base.h"
#include "swoc/Lexicon.h"

namespace swoc
{
// Handy template for constructing static error reporting strings.
template <typename... Args>
std::string
bwstring(swoc::TextView fmt, Args &&... args)
{
  std::string s;
  bwprint_v(s, fmt, std::forward_as_tuple(args...));
  return std::move(s);
}

template <typename T, size_t N>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, std::array<T, N> const &array)
{
  auto pos = w.extent();
  for (T const &value : array) {
    if (pos != w.extent()) {
      w.write(", ");
      bwformat(w, spec, value);
    }
  }
  return w;
}

namespace bwf
{
  template <typename T> struct LexiconPrimaryNamesWrapper {
    swoc::Lexicon<T> const &_value;
  };

  template <typename T>
  LexiconPrimaryNamesWrapper<T>
  LexiconPrimaryNames(Lexicon<T> const &lexicon)
  {
    return LexiconPrimaryNamesWrapper<T>{lexicon};
  }

}; // namespace bwf

template <typename T>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, bwf::LexiconPrimaryNamesWrapper<T> const &lexicon)
{
  auto pos = w.extent();
  for ([[maybe_unused]] auto const &[value, name] : lexicon._value) {
    if (pos != w.extent()) {
      w.write(", ");
      bwformat(w, spec, name);
    }
  }
  return w;
}

} // namespace swoc
