/**
  @file AtomicBit

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

  @section details Details

////////////////////////////////////////////
  Implements class AtomicBit
*/

#pragma once
#include <stdint.h>
#include <atomic>

//////////////////////////////////////////////////////
/// AtomicBit for inplace atomic bit operations
/* useful when you reference a bit packed into a byte (unit_8) as a bool&,
 * you want a bit to 'walk and talk' like an std::atomic<bool> or std::atomic_flag.
 * In practice this is constructed at time of the operation(s),
 * storing it would defeat the purpose of packing the bits.
 */
class AtomicBit
{
  std::atomic<uint8_t> *_byte_ptr; ///< pointer to the byte
  uint8_t const _mask;             ///< bitmask of which bit you are using

public:
  // define a bit to perform atomic operations
  AtomicBit(std::atomic<uint8_t> &byte, const uint8_t mask) : _byte_ptr(&byte), _mask(mask) {}
  AtomicBit(uint8_t *byte_ptr, const uint8_t mask) : _byte_ptr(reinterpret_cast<std::atomic<uint8_t> *>(byte_ptr)), _mask(mask) {}

  // Atomically set the bit true
  // return @c true if the bit was changed, @c false if not.
  bool
  test_and_set()
  {
    return compare_exchange(true);
  }

  // allows assign by bool
  // @return The new value of the bit.
  bool
  operator=(bool val)
  {
    compare_exchange(val);
    return val;
  }

  // allow cast to bool
  explicit operator bool() const { return (*_byte_ptr) & _mask; }

  // allows compare with bool
  bool
  operator==(bool rhs) const
  {
    return bool(*this) == rhs;
  }

  // Atomically set the bit to `val`
  // return @c true if the bit was changed, @c false if not.
  bool
  compare_exchange(bool val)
  {
    while (true) {
      uint8_t byte_val            = *_byte_ptr;
      const uint8_t next_byte_val = val ? (byte_val | _mask) : (byte_val & ~_mask);
      if (byte_val == next_byte_val) {
        return false;
      }
      if (_byte_ptr->compare_exchange_weak(byte_val, next_byte_val)) {
        return true;
      }
    }
  }
};
