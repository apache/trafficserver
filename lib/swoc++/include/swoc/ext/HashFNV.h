/** @file

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
  See the NOTICE file distributed with this work for additional information regarding copyright
  ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance with the License.  You may obtain a
  copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

/*
  http://www.isthe.com/chongo/tech/comp/fnv/

  Currently implemented FNV-1a 32bit and FNV-1a 64bit
 */

#pragma once

#include <cstdint>
#include "swoc/TextView.h"

namespace swoc
{
struct Hash32FNV1a {
protected:
  using self_type                = Hash32FNV1a;
  static constexpr uint32_t INIT = 0x811c9dc5u;

public:
  using value_type = uint32_t;

  Hash32FNV1a() = default;

  self_type &update(std::string_view const &data);

  self_type & final();

  value_type get() const;

  self_type &clear();

  template <typename X, typename V> self_type &update(TransformView<X, V> view);

  template <typename X, typename V> value_type hash_immediate(TransformView<X, V> const &view);

  value_type hash_immediate(std::string_view const &data);

private:
  value_type hval{INIT};
};

struct Hash64FNV1a {
protected:
  using self_type                = Hash64FNV1a;
  static constexpr uint64_t INIT = 0xcbf29ce484222325ull;

public:
  using value_type = uint64_t;

  Hash64FNV1a() = default;

  self_type &update(std::string_view const &data);

  self_type & final();

  value_type get() const;

  self_type &clear();

  template <typename X, typename V> self_type &update(TransformView<X, V> view);

  template <typename X, typename V> value_type hash_immediate(TransformView<X, V> const &view);

  value_type hash_immediate(std::string_view const &data);

private:
  value_type hval{INIT};
};

// ----------
// Implementation

// -- 32 --

inline auto
Hash32FNV1a::clear() -> self_type &
{
  hval = INIT;
  return *this;
}

template <typename X, typename V>
auto
Hash32FNV1a::update(TransformView<X, V> view) -> self_type &
{
  for (; view; ++view) {
    hval ^= static_cast<value_type>(*view);
    hval += (hval << 1) + (hval << 4) + (hval << 7) + (hval << 8) + (hval << 24);
  }
  return *this;
}

inline auto
Hash32FNV1a::update(std::string_view const &data) -> self_type &
{
  return this->update(transform_view_of(data));
}

inline auto
Hash32FNV1a::final() -> self_type &
{
  return *this;
}

inline auto
Hash32FNV1a::get() const -> value_type
{
  return hval;
}

template <typename X, typename V>
auto
Hash32FNV1a::hash_immediate(swoc::TransformView<X, V> const &view) -> value_type
{
  return this->update(view).get();
}

inline auto
Hash32FNV1a::hash_immediate(std::string_view const &data) -> value_type
{
  return this->update(data).final().get();
}

// -- 64 --

inline auto
Hash64FNV1a::clear() -> self_type &
{
  hval = INIT;
  return *this;
}

template <typename X, typename V>
auto
Hash64FNV1a::update(TransformView<X, V> view) -> self_type &
{
  for (; view; ++view) {
    hval ^= static_cast<value_type>(*view);
    hval += (hval << 1) + (hval << 4) + (hval << 5) + (hval << 7) + (hval << 8) + (hval << 40);
  }
  return *this;
}

inline auto
Hash64FNV1a::update(std::string_view const &data) -> self_type &
{
  return this->update(transform_view_of(data));
}

inline auto
Hash64FNV1a::final() -> self_type &
{
  return *this;
}

inline auto
Hash64FNV1a::get() const -> value_type
{
  return hval;
}

template <typename X, typename V>
auto
Hash64FNV1a::hash_immediate(swoc::TransformView<X, V> const &view) -> value_type
{
  return this->update(view).final().get();
}

inline auto
Hash64FNV1a::hash_immediate(std::string_view const &data) -> value_type
{
  return this->update(data).final().get();
}

} // namespace swoc
