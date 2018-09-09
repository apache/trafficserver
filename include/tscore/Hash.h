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

#pragma once

#include <cstddef>
#include <cstdint>
#include <cctype>

struct ATSHashBase {
  virtual ATSHashBase &update(const void *, size_t) = 0;
  virtual ATSHashBase & final(void)                 = 0;
  virtual ATSHashBase &clear(void)                  = 0;
  virtual ~ATSHashBase();
};

struct ATSHash : ATSHashBase {
  struct nullxfrm {
    uint8_t
    operator()(uint8_t byte) const
    {
      return byte;
    }
  };

  struct nocase {
    uint8_t
    operator()(uint8_t byte) const
    {
      return toupper(byte);
    }
  };

  virtual const void *get(void) const = 0;
  virtual size_t size(void) const     = 0;
  virtual bool operator==(const ATSHash &) const;
};

struct ATSHash32 : ATSHashBase {
protected:
  using self_type = ATSHash32;

public:
  using value_type = uint32_t;

  // Co-vary the return type.
  virtual self_type &update(const void *, size_t) override = 0;
  virtual self_type & final(void) override                 = 0;
  virtual self_type &clear(void) override                  = 0;

  virtual value_type get(void) const = 0;
  virtual bool operator==(const ATSHash32 &) const;
  value_type hash_immediate(void *data, size_t len);
};

struct ATSHash64 : ATSHashBase {
protected:
  using self_type = ATSHash64;

public:
  using value_type = uint64_t;

  // Co-vary the return type.
  virtual self_type &update(const void *, size_t) override = 0;
  virtual self_type & final(void) override                 = 0;
  virtual self_type &clear(void) override                  = 0;

  virtual value_type get(void) const = 0;
  virtual bool operator==(const ATSHash64 &) const;
  value_type hash_immediate(void *data, size_t len);
};

// ----
// Implementation

inline ATSHash32::value_type
ATSHash32::hash_immediate(void *data, size_t len)
{
  return this->update(data, len).final().get();
}

inline ATSHash64::value_type
ATSHash64::hash_immediate(void *data, size_t len)
{
  return this->update(data, len).final().get();
}
