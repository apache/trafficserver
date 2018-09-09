/** @file

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this fileN
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

/*
  http://www.isthe.com/chongo/tech/comp/fnv/

  Currently implemented FNV-1a 32bit and FNV-1a 64bit
 */

#pragma once

#include "tscore/Hash.h"
#include <cstdint>

struct ATSHash32FNV1a : ATSHash32 {
protected:
  using super_type = ATSHash32;
  using nullxfrm   = ATSHash::nullxfrm;

public:
  ATSHash32FNV1a(void);

  template <typename Transform> void update(const void *data, size_t len, const Transform &xf);

  void update(const void *data, size_t len) override;

  void final(void) override;
  uint32_t get(void) const override;
  void clear(void) override;

  template <typename Transform> uint32_t hash_immediate(const void *data, size_t len, const Transform &xf);

private:
  uint32_t hval;
};

struct ATSHash64FNV1a : ATSHash64 {
  ATSHash64FNV1a(void);

  template <typename Transform> void update(const void *data, size_t len, Transform xfrm);
  void update(const void *data, size_t len) override;

  void final(void) override;
  uint64_t get(void) const override;
  void clear(void) override;

  template <typename Transform> uint64_t hash_immediate(const void *data, size_t len, const Transform &xf);

private:
  uint64_t hval;
};

// ----------
// Implementation

inline void
ATSHash32FNV1a::update(const void *data, size_t len)
{
  return this->update(data, len, ATSHash::nullxfrm());
}
inline void
ATSHash64FNV1a::update(const void *data, size_t len)
{
  return this->update(data, len, ATSHash::nullxfrm());
}

template <typename Transform>
uint32_t
ATSHash32FNV1a::hash_immediate(const void *data, size_t len, const Transform &xf)
{
  this->update(data, len, xf);
  this->final();
  return this->get();
}

template <typename Transform>
void
ATSHash32FNV1a::update(const void *data, size_t len, const Transform &xf)
{
  const uint8_t *bp = static_cast<const uint8_t *>(data);
  const uint8_t *be = bp + len;

  for (; bp < be; ++bp) {
    hval ^= static_cast<uint32_t>(xf(*bp));
    hval += (hval << 1) + (hval << 4) + (hval << 7) + (hval << 8) + (hval << 24);
  }
}

template <typename Transform>
void
ATSHash64FNV1a::update(const void *data, size_t len, Transform xf)
{
  const uint8_t *bp = static_cast<const uint8_t *>(data);
  const uint8_t *be = bp + len;

  for (; bp < be; ++bp) {
    hval ^= static_cast<uint64_t>(xf(*bp));
    hval += (hval << 1) + (hval << 4) + (hval << 5) + (hval << 7) + (hval << 8) + (hval << 40);
  }
}
