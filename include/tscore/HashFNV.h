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
#include "tscore/Hash.h"

struct ATSHash32FNV1a : ATSHash32 {
protected:
  using self_type                = ATSHash32FNV1a;
  using super_type               = ATSHash32;
  static constexpr uint32_t INIT = 0x811c9dc5u;

public:
  ATSHash32FNV1a() = default;

  template <typename Transform> self_type &update(const void *data, size_t len, const Transform &xf);

  self_type &update(const void *data, size_t len) override;

  self_type & final() override;
  value_type get() const override;
  self_type &clear() override;

  template <typename Transform> uint32_t hash_immediate(const void *data, size_t len, const Transform &xf);

private:
  value_type hval{INIT};
};

struct ATSHash64FNV1a : ATSHash64 {
protected:
  using self_type                = ATSHash64FNV1a;
  using super_type               = ATSHash64;
  static constexpr uint64_t INIT = 0xcbf29ce484222325ull;

public:
  ATSHash64FNV1a() = default;

  template <typename Transform> self_type &update(const void *data, size_t len, const Transform &xf);
  self_type &update(const void *data, size_t len) override;

  self_type & final() override;
  value_type get() const override;
  self_type &clear() override;

  template <typename Transform> value_type hash_immediate(const void *data, size_t len, const Transform &xf);

private:
  value_type hval{INIT};
};

// ----------
// Implementation

// -- 32 --

inline auto
ATSHash32FNV1a::clear() -> self_type &
{
  hval = INIT;
  return *this;
}

template <typename Transform>
auto
ATSHash32FNV1a::update(const void *data, size_t len, const Transform &xf) -> self_type &
{
  const uint8_t *bp = static_cast<const uint8_t *>(data);
  const uint8_t *be = bp + len;

  for (; bp < be; ++bp) {
    hval ^= static_cast<uint32_t>(xf(*bp));
    hval += (hval << 1) + (hval << 4) + (hval << 7) + (hval << 8) + (hval << 24);
  }
  return *this;
}

inline auto
ATSHash32FNV1a::update(const void *data, size_t len) -> self_type &
{
  return this->update(data, len, ATSHash::nullxfrm());
}

inline auto
ATSHash32FNV1a::final() -> self_type &
{
  return *this;
}

inline auto
ATSHash32FNV1a::get() const -> value_type
{
  return hval;
}

// -- 64 --

inline auto
ATSHash64FNV1a::clear() -> self_type &
{
  hval = INIT;
  return *this;
}

template <typename Transform>
auto
ATSHash64FNV1a::update(const void *data, size_t len, const Transform &xf) -> self_type &
{
  const uint8_t *bp = static_cast<const uint8_t *>(data);
  const uint8_t *be = bp + len;

  for (; bp < be; ++bp) {
    hval ^= static_cast<uint64_t>(xf(*bp));
    hval += (hval << 1) + (hval << 4) + (hval << 5) + (hval << 7) + (hval << 8) + (hval << 40);
  }
  return *this;
}

inline auto
ATSHash64FNV1a::update(const void *data, size_t len) -> self_type &
{
  return this->update(data, len, ATSHash::nullxfrm());
}

template <typename Transform>
auto
ATSHash32FNV1a::hash_immediate(const void *data, size_t len, const Transform &xf) -> value_type
{
  return this->update(data, len, xf).final().get();
}

inline auto
ATSHash64FNV1a::final() -> self_type &
{
  return *this;
}

inline auto
ATSHash64FNV1a::get() const -> value_type
{
  return hval;
}
