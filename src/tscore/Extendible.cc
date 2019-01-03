/** @file

  Extendible

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
  Implements:
  * Extendible<Derived_t>
  * Schema
  * fieldAdd
  * fieldFind
 */

#include "tscore/Extendible.h"

namespace ext
{
namespace details
{
  ////////////////////////////////////////////////////
  // Schema Methods

  Schema::~Schema() {}

  void
  Schema::updateMemOffsets()
  {
    ink_release_assert(instance_count == 0);

    uint32_t acc_offset = 0;
    alloc_align         = 1;

    for (auto &pair_fld : fields) {
      alloc_align = std::max(alloc_align, pair_fld.second.align);
    }

    // allocate fields from largest to smallest alignment
    uint8_t processing_align = alloc_align;
    while (processing_align > 0) {
      uint8_t next_align = 0;
      for (auto &pair_fld : fields) {
        auto &fld = pair_fld.second;
        if (fld.align == processing_align) {
          fld.field_offset = acc_offset;
          acc_offset += fld.size;
        } else if (fld.align < processing_align) {
          next_align = std::max(next_align, fld.align);
        }
      }
      processing_align = next_align;
    }

    // align '0' are packed bit allocations.
    uint32_t acc_bit_offset = 0;
    for (auto &pair_fld : fields) {
      auto &fld = pair_fld.second;
      if (fld.align == 0) {
        fld.field_offset = acc_offset + acc_bit_offset / 8;
        fld.mask         = 1 << (acc_bit_offset % 8);
        ++acc_bit_offset;
      }
    }

    alloc_size = acc_offset + (acc_bit_offset + 7) / 8;
  }

  bool
  Schema::reset()
  {
    if (instance_count > 0) {
      // free instances before calling this so we don't leak memory
      return false;
    }
    fields.clear();
    updateMemOffsets();
    return true;
  }

  void
  Schema::callConstructor(uintptr_t ext_loc)
  {
    ink_assert(ext_loc);
    ++instance_count; // don't allow schema modification
    // init all extendible memory to 0, incase constructors don't
    memset(reinterpret_cast<void *>(ext_loc), 0, alloc_size);

    for (auto const &elm : fields) {
      if (elm.second.constructor) {
        elm.second.constructor(FieldPtr(ext_loc + elm.second.field_offset));
      }
    }
  }

  void
  Schema::callDestructor(uintptr_t ext_loc)
  {
    ink_assert(ext_loc);
    for (auto const &elm : fields) {
      if (elm.second.destructor) {
        elm.second.destructor(FieldPtr(ext_loc + elm.second.field_offset));
      }
    }
    --instance_count;
  }

  size_t
  Schema::fullSize(const size_t base_size) const
  {
    ink_assert(base_size);
    return ROUNDUP(base_size, alloc_align) + alloc_size;
  }

  bool
  Schema::no_instances() const
  {
    return instance_count == 0;
  }
} // namespace details

} // namespace ext
