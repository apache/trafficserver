/** @file

  SharedAccess

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
  Implements SharedExtendible<Derived_t>:
  * SharedExtendible<Derived_t>::Schema
  * SharedExtendible<Derived_t>::FieldSchema
  * SharedExtendible<Derived_t>::FieldId<Access_t,Field_t>
 */

#pragma once
#include "stdint.h"
#include <cstddef>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <type_traits>

#include "ts/ink_memory.h"
#include "ts/ink_assert.h"
#include "SharedAccess.h"

#ifndef ROUNDUP
#define ROUNDUP(x, y) ((((x) + ((y)-1)) / (y)) * (y))
#endif

using SharedOffset_t = uint16_t;

// used for C API
typedef const void *FieldId_c;

/// all types must allow unblocking MT read access
enum SharedAccessEnum { ATOMIC, BIT, CONST, COPYSWAP, C_API, NUM_ACCESS_TYPES };

/**
 * @brief Allows code (and Plugins) to declare member variables during system init.
 *
 * The size of this structure is actually zero, so it will not change the size of your derived class.
 * But new and delete are overriden to use allocate enough bytes of the derived type + added fields.
 * All bool's are packed to save space using the *Bit methods.
 * This API is focused on thread safe data types that allow minimally blocked reading.
 * This is templated so static variables are instanced per Derived type. B/c we need to have different
 * size field sets.
 *
 * @tparam Derived_t - the class that you want to extend at runtime.
 *
 * @see test_SharedExtendible.cc for examples
 *
 */
template <typename Derived_t> struct SharedExtendible {
  using SharedExtendible_t = SharedExtendible<Derived_t>;

  /////////////////////////////////////////////////////////////////////
  /// strongly type the FieldId to avoid user error and branching logic
  template <SharedAccessEnum FieldAccess_e, typename Field_t> class FieldId
  {
  private:
    SharedOffset_t const *offset_ptr;

  public:
    using FieldId_t = FieldId<FieldAccess_e, Field_t>;

    FieldId() { offset_ptr = nullptr; }

    static FieldId_t
    find(std::string const &field_name)
    {
      auto field_iter = schema.fields.find(field_name);
      ink_release_assert(field_iter != schema.fields.end());
      FieldSchema &fs = field_iter->second;
      ink_release_assert(fs.access == FieldAccess_e);
      return FieldId_t(fs.offset);
    }

    // only allow offest to be used by schema code
    friend SharedExtendible_t;

  private:
    FieldId(SharedOffset_t const &offset) { offset_ptr = &offset; }
    SharedOffset_t
    offset() const
    {
      return *offset_ptr;
    }
  };
  using BitFieldId = FieldId<BIT, bool>;

  /////////////////////////////////////////////////////////////////////
  /// defines a runtime "member variable", element of the blob
  struct FieldSchema {
    using Func_t = void(void *);

    SharedAccessEnum access; ///< which API is used to access the data
    SharedOffset_t size;     ///< size of field
    SharedOffset_t offset;   ///< offset of field from 'this'
    Func_t *construct_fn;    ///< the data type's constructor
    Func_t *destruct_fn;     ///< the data type's destructor
  };

  /////////////////////////////////////////////////////////////////////
  /// manages the fields as data structures
  class Schema
  {
    friend SharedExtendible_t;

  private:
    std::unordered_map<std::string, FieldSchema> fields; ///< defined elements of the blob by name
    uint32_t bit_offset;                                 ///< offset to first bit
    size_t alloc_size;                                   ///< bytes to allocate
    size_t alloc_align;                                  ///< alignment for each allocation
    std::atomic_uint instance_count;                     ///< the number of class instances in use.

  public:
    /// Constructor
    Schema() : instance_count(0) { reset(); }

    /// Add a new Field to this record type
    template <SharedAccessEnum Access_t, typename Field_t>
    bool
    addField(FieldId<Access_t, Field_t> &field_id, std::string const &field_name)
    {
      static_assert(Access_t == BIT || std::is_same<Field_t, bool>::value == false,
                    "Use BitField so we can pack bits, they are still atomic.");
      ink_release_assert(instance_count == 0); // it's too late, we already started allocating.

      SharedOffset_t size = 0;
      switch (Access_t) {
      case BIT: {
        size = 0;
      } break;
      case ATOMIC: {
        size        = std::max(sizeof(std::atomic<Field_t>), alignof(std::atomic<Field_t>));
        alloc_align = std::max(alloc_align, alignof(std::atomic<Field_t>));
      } break;
      default:
        size = sizeof(Field_t);
      }

      // capture the default constructors of the data type
      static auto construct_fn = [](void *ptr) { new (ptr) Field_t; };
      static auto destruct_fn  = [](void *ptr) { static_cast<Field_t *>(ptr)->~Field_t(); };

      fields[field_name]  = FieldSchema{Access_t, size, 0, construct_fn, destruct_fn};
      field_id.offset_ptr = &fields[field_name].offset;
      updateMemOffsets();
      return true;
    }

    /// Add a new Field to this record type
    template <typename Field_t>
    bool
    addField(FieldId<COPYSWAP, Field_t> &field_id, std::string const &field_name)
    {
      static_assert(std::is_copy_constructible<Field_t>::value == true, "Must have a copy constructor to use copyswap.");
      ink_release_assert(instance_count == 0); // it's too late, we already started allocating.
      using ptr_t         = std::shared_ptr<Field_t>;
      SharedOffset_t size = sizeof(ptr_t);

      // capture the default constructors of the data type
      static auto construct_fn = [](void *ptr) {
        auto p = new (ptr) ptr_t;
        *p     = std::make_shared<Field_t>();
      };
      static auto destruct_fn = [](void *ptr) { static_cast<ptr_t *>(ptr)->~ptr_t(); };

      fields[field_name]  = FieldSchema{COPYSWAP, size, 0, construct_fn, destruct_fn};
      field_id.offset_ptr = &fields[field_name].offset;
      updateMemOffsets();
      return true;
    }

    /// Add a new Field to this record type (for a C API)
    FieldId_c
    addField_c(char const *field_name, size_t size, void (*construct_fn)(void *), void (*destruct_fn)(void *))
    {
      ink_release_assert(size == 1 || size == 2 || size == 4 || size % 8 == 0); // must use aligned sizes
      ink_release_assert(instance_count == 0);                                  // it's too late, we already started allocating.
      const std::string field_name_str(field_name);
      fields[field_name_str] = FieldSchema{C_API, static_cast<SharedOffset_t>(size), 0, construct_fn, destruct_fn};
      updateMemOffsets();
      return &fields[field_name].offset;
    }

    FieldId_c
    find_c(char const *field_name)
    {
      auto field_iter = fields.find(field_name);
      ink_release_assert(field_iter != fields.end());
      FieldSchema &fs = field_iter->second;
      ink_release_assert(fs.access == C_API);
      return &fs.offset;
    }

    void
    updateMemOffsets()
    {
      ink_release_assert(instance_count == 0);

      uint32_t acc_offset = ROUNDUP(sizeof(Derived_t), alloc_align);

      SharedOffset_t size_blocks[] = {4, 2, 1};

      for (auto &pair_fld : fields) {
        auto &fld = pair_fld.second;
        if (fld.size >= 8) {
          fld.offset = acc_offset;
          acc_offset += fld.size;
        }
      }
      for (auto sz : size_blocks) {
        for (auto &pair_fld : fields) {
          auto &fld = pair_fld.second;
          if (fld.size == sz) {
            fld.offset = acc_offset;
            acc_offset += fld.size;
          }
        }
      }
      bit_offset              = acc_offset;
      uint32_t acc_bit_offset = 0;
      for (auto &pair_fld : fields) {
        auto &fld = pair_fld.second;
        if (fld.size == 0) {
          fld.offset = acc_bit_offset;
          ++acc_bit_offset;
        }
      }

      alloc_size = acc_offset + (acc_bit_offset + 7) / 8; // size '0' are packed bit allocations.
    }

    bool
    reset()
    {
      if (instance_count > 0) {
        // free instances before calling this so we don't leak memory
        return false;
      }
      fields.clear();
      alloc_size  = sizeof(Derived_t);
      alloc_align = alignof(Derived_t);
      return true;
    }

    void
    call_construct(char *ext_as_char_ptr)
    {
      ++instance_count; // don't allow schema modification
      // init all extendible memory to 0, incase constructors don't
      memset(ext_as_char_ptr + sizeof(Derived_t), 0, alloc_size - sizeof(Derived_t));

      for (auto const &elm : fields) {
        FieldSchema const &field_schema = elm.second;
        if (field_schema.access != BIT && field_schema.construct_fn != nullptr) {
          field_schema.construct_fn(ext_as_char_ptr + field_schema.offset);
        }
      }
    }

    void
    call_destruct(char *ext_as_char_ptr)
    {
      for (auto const &elm : fields) {
        FieldSchema const &field_schema = elm.second;
        if (field_schema.access != BIT && field_schema.destruct_fn != nullptr) {
          field_schema.destruct_fn(ext_as_char_ptr + field_schema.offset);
        }
      }
      --instance_count;
    }
    size_t
    size() const
    {
      return alloc_size;
    }
    bool
    no_instances() const
    {
      return instance_count == 0;
    }

  }; // end Schema struct

  //////////////////////////////////////////
  // SharedExtendible static data
  /// one schema instance per Derived_t to define contained fields
  static Schema schema;

  //////////////////////////////////////////
  /// SharedExtendible Methods

  /// return a reference to an atomic field (read, write or other atomic operation)
  template <typename Field_t>
  std::atomic<Field_t> &
  get(FieldId<ATOMIC, Field_t> const &field)
  {
    return *at_offset<std::atomic<Field_t> *>(field.offset());
  }

  /// atomically read a bit value
  bool const
  get(BitFieldId field) const
  {
    return readBit(field);
  }

  /// atomically read a bit value
  bool const
  readBit(BitFieldId field) const
  {
    const char &c   = *(at_offset<char const *>(schema.bit_offset) + field.offset() / 8);
    const char mask = 1 << (field.offset() % 8);
    return (c & mask) != 0;
  }

  /// atomically write a bit value
  void
  writeBit(BitFieldId field, bool const val)
  {
    char &c         = *(at_offset<char *>(schema.bit_offset) + field.offset() / 8);
    const char mask = 1 << (field.offset() % 8);
    if (val) {
      c |= mask;
    } else {
      c &= ~mask;
    }
  }

  /// return a reference to an const field
  template <typename Field_t>
  Field_t const & // value is not expected to change, or be freed while 'this' exists.
  get(FieldId<CONST, Field_t> field) const
  {
    return *at_offset<const Field_t *>(field.offset());
  }

  /// return a reference to an const field that is non-const for initialization purposes
  template <typename Field_t>
  Field_t &
  writeConst(FieldId<CONST, Field_t> field)
  {
    return *at_offset<Field_t *>(field.offset());
  }

  /// return a shared pointer to last committed field value
  template <typename Field_t>
  const std::shared_ptr<Field_t> // shared_ptr so the value can be updated while in use.
  get(FieldId<COPYSWAP, Field_t> field) const
  {
    auto data_ptr = at_offset<const std::shared_ptr<Field_t> *>(field.offset());
    WriterLock_t access_lock(SharedAccessMutex(data_ptr));
    return *data_ptr;
    //[end scope] release access_lock
  }

  /// return a writer created from the last committed field value
  template <typename Field_t>
  writer_ptr<Field_t>
  writeCopySwap(FieldId<COPYSWAP, Field_t> field)
  {
    auto data_ptr = at_offset<std::shared_ptr<Field_t> *>(field.offset());
    return writer_ptr<Field_t>(*data_ptr);
  }

  // operator[]
  template <SharedAccessEnum Access_t, typename Field_t> auto operator[](FieldId<Access_t, Field_t> field) { return get(field); }

  //////////////////////////////////////////
  // C API
  void *
  get_c(FieldId_c &field)
  {
    return at_offset<void *>(*static_cast<SharedOffset_t const *>(field));
  }

  //////////////////////////////////////////
  // lifetime management

  /// allocate a new object with properties
  void *
  operator new(size_t size)
  {
    // allocate one block for all the memory, including the derived_t members
    // return ::operator new(schema.alloc_size);
    void *ptr = ats_memalign(schema.alloc_align, schema.alloc_size);
    ink_release_assert(ptr != nullptr);
    return ptr;
  }

  /// construct all fields
  SharedExtendible() { schema.call_construct(this_as_char_ptr()); }

  /// don't allow copy construct, that doesn't allow atomicity
  SharedExtendible(SharedExtendible &) = delete;

  /// destruct all fields
  ~SharedExtendible() { schema.call_destruct(this_as_char_ptr()); }

private:
  char *
  this_as_char_ptr()
  {
    return static_cast<char *>(static_cast<void *>(this));
  }
  char const *
  this_as_char_ptr() const
  {
    return static_cast<char const *>(static_cast<void const *>(this));
  }

  template <typename Return_t>
  Return_t
  at_offset(SharedOffset_t offset)
  {
    return reinterpret_cast<Return_t>(this_as_char_ptr() + offset);
  }
  template <typename Return_t>
  Return_t
  at_offset(SharedOffset_t offset) const
  {
    return reinterpret_cast<Return_t>(this_as_char_ptr() + offset);
  }
};

// define the static schema per derived type
template <typename Derived_t> typename SharedExtendible<Derived_t>::Schema SharedExtendible<Derived_t>::schema;
