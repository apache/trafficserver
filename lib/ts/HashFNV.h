/** @file

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

/*
  http://www.isthe.com/chongo/tech/comp/fnv/

  Currently implemented FNV-1a 32bit and FNV-1a 64bit
 */

#ifndef __HASH_FNV_H__
#define __HASH_FNV_H__

#include "ts/Hash.h"
#include <stdint.h>

struct ATSHash32FNV1a : ATSHash32 {
  ATSHash32FNV1a(void);

  template <typename Transform> void update(const void *data, size_t len, Transform xfrm);
  void
  update(const void *data, size_t len)
  {
    update(data, len, ATSHash::nullxfrm());
  }

  void final(void);
  uint32_t get(void) const;
  void clear(void);

private:
  uint32_t hval;
};

template <typename Transform>
void
ATSHash32FNV1a::update(const void *data, size_t len, Transform xfrm)
{
  uint8_t *bp = (uint8_t *)data;
  uint8_t *be = bp + len;

  for (; bp < be; ++bp) {
    hval ^= (uint32_t)xfrm(*bp);
    hval += (hval << 1) + (hval << 4) + (hval << 7) + (hval << 8) + (hval << 24);
  }
}

struct ATSHash64FNV1a : ATSHash64 {
  ATSHash64FNV1a(void);

  template <typename Transform> void update(const void *data, size_t len, Transform xfrm);
  void
  update(const void *data, size_t len)
  {
    update(data, len, ATSHash::nullxfrm());
  }

  void final(void);
  uint64_t get(void) const;
  void clear(void);

private:
  uint64_t hval;
};

template <typename Transform>
void
ATSHash64FNV1a::update(const void *data, size_t len, Transform xfrm)
{
  uint8_t *bp = (uint8_t *)data;
  uint8_t *be = bp + len;

  for (; bp < be; ++bp) {
    hval ^= (uint64_t)xfrm(*bp);
    hval += (hval << 1) + (hval << 4) + (hval << 5) + (hval << 7) + (hval << 8) + (hval << 40);
  }
}

#endif
