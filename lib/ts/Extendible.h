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
  Implements Extendible<Derived_t>:
  * Extendible<Derived_t>::Schema
  * Extendible<Derived_t>::FieldSchema
  * Extendible<Derived_t>::FieldId<Access_t,Field_t>
 */

#pragma once
#include "stdint.h"
#include <typeinfo>
#include <typeindex>
#include <cstddef>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <type_traits>

#include "ts/ink_memory.h"
#include "ts/ink_assert.h"
#include "AcidPtr.h"

#ifndef ROUNDUP
#define ROUNDUP(x, y) ((((x) + ((y)-1)) / (y)) * (y))
#endif

// used for C API
typedef const void *FieldId_C;

namespace MT // (multi-threaded)
{
/// used to store byte offsets to fields
using ExtendibleOffset_t = uint16_t;
/// all types must allow unblocking MT read access
enum AccessEnum { ATOMIC, BIT, STATIC, ACIDPTR, DIRECT, C_API, NUM_ACCESS_TYPES };

inline bool &
areStaticsFrozen()
{
  static bool frozen = 0;
  return frozen;
}

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
 * @see test_Extendible.cc for examples
 *
 */
template <typename Derived_t> struct Extendible {
  //////////////////////////////////////////
  // Internal classes
  template <AccessEnum FieldAccess_e, typename Field_t> class FieldId; // field handle (strongly type)
  struct FieldSchema;                                                  // field descriptor
  class Schema;                                                        // memory layout, field container
  // aliases
  using BitFieldId = FieldId<BIT, bool>;

  //////////////////////////////////////////
  // Extendible static data
  /// one schema instance per Derived_t to define contained fields
  static Schema schema;

  //////////////////////////////////////////
  // Extendible member variables

  // none
  // this uses a single alloc and type erasing

  //////////////////////////////////////////
  // Extendible lifetime management
  /** don't allow copy construct, that doesn't allow atomicity */
  Extendible(Extendible &) = delete;
  /** allocate a new object with additional field data */
  void *operator new(size_t size);
  /** construct all fields */
  Extendible() { schema.call_construct(this_as_char_ptr()); }
  /** destruct all fields */
  ~Extendible() { schema.call_destruct(this_as_char_ptr()); }

  //////////////////////////////////////////
  /// Extendible member methods

  /// ATOMIC API - atomic field reference (read, write or other atomic operation)
  template <typename Field_t> std::atomic<Field_t> &get(FieldId<ATOMIC, Field_t> const &field);

  /// BIT API - compressed boolean fields
  bool const get(BitFieldId field) const;
  bool const readBit(BitFieldId field) const;
  void writeBit(BitFieldId field, bool const val);

  /// STATIC API - immutable field, value is not expected to change, no internal thread safety
  template <typename Field_t> Field_t const &get(FieldId<STATIC, Field_t> field) const;
  template <typename Field_t> Field_t &init(FieldId<STATIC, Field_t> field);

  /// ACIDPTR API - returns a const shared pointer to last committed field value
  template <typename Field_t> std::shared_ptr<const Field_t> get(FieldId<ACIDPTR, Field_t> field) const;
  template <typename Field_t> AcidCommitPtr<Field_t> writeAcidPtr(FieldId<ACIDPTR, Field_t> field);

  /// DIRECT API -  mutable field, no internal thread safety, expected to be performed externally.
  template <typename Field_t> Field_t const &get(FieldId<DIRECT, Field_t> field) const;
  template <typename Field_t> Field_t &get(FieldId<DIRECT, Field_t> field);

  /// C API - returns pointer, no internal thread safety
  void *get(FieldId_C &field);

  // operator[]
  template <AccessEnum Access_t, typename Field_t> auto operator[](FieldId<Access_t, Field_t> field) { return get(field); }

  /////////////////////////////////////////////////////////////////////
  /// defines a runtime "member variable", element of the blob
  struct FieldSchema {
    using Func_t = std::function<void(void *)>;

    AccessEnum access         = NUM_ACCESS_TYPES;              ///< which API is used to access the data
    std::type_index type      = std::type_index(typeid(void)); ///< datatype
    ExtendibleOffset_t size   = 0;                             ///< size of field
    ExtendibleOffset_t offset = 0;                             ///< offset of field from 'this'
    Func_t construct_fn       = nullptr;                       ///< the data type's constructor
    Func_t destruct_fn        = nullptr;                       ///< the data type's destructor
  };

  /////////////////////////////////////////////////////////////////////
  /// manages the a static layout of fields as data structures
  class Schema
  {
    friend Extendible; // allow direct access for internal classes

  private:
    std::unordered_map<std::string, FieldSchema> fields;  ///< defined elements of the blob by name
    uint32_t bit_offset             = 0;                  ///< offset to first bit
    size_t alloc_size               = sizeof(Derived_t);  ///< bytes to allocate
    size_t alloc_align              = alignof(Derived_t); ///< alignment for each allocation
    std::atomic_uint instance_count = {0};                ///< the number of Extendible<Derived> instances in use.

  public:
    Schema() {}

    /// Add a new Field to this record type
    template <AccessEnum Access_t, typename Field_t>
    bool addField(FieldId<Access_t, Field_t> &field_id, std::string const &field_name);

    template <typename Field_t> bool addField(FieldId<ACIDPTR, Field_t> &field_id, std::string const &field_name);
    template <AccessEnum Access_t, typename Field_t> class FieldId<Access_t, Field_t> find(std::string const &field_name);

    /// Add a new Field to this record type (for a C API)

    FieldId_C addField_C(char const *field_name, size_t size, void (*construct_fn)(void *), void (*destruct_fn)(void *));
    FieldId_C find_C(char const *field_name); ///< C_API returns an existing fieldId

    /// Testing methods
    size_t size() const;       ///< returns sizeof memory allocated
    bool no_instances() const; ///< returns true if there are no instances of Extendible<Derived_t>
    bool reset();              ///< clears all field definitions.

  protected:
    /// Internal methods
    void updateMemOffsets();                    ///< updates memory offsets, alignment, and total allocation size
    void call_construct(char *ext_as_char_ptr); ///< calls constructor for each field
    void call_destruct(char *ext_as_char_ptr);  ///< call destructor for each field

  }; // end Schema struct

private:
  // Extendible convience methods
  char *this_as_char_ptr();
  char const *this_as_char_ptr() const;

  template <typename Return_t> Return_t at_offset(ExtendibleOffset_t offset);
  template <typename Return_t> Return_t at_offset(ExtendibleOffset_t offset) const;
};

// define the static schema per derived type
template <typename Derived_t> typename Extendible<Derived_t>::Schema Extendible<Derived_t>::schema;

/////////////////////////////////////////////////////////////////////
// class FieldId
//
/// strongly type the FieldId to avoid user error and branching logic
template <typename Derived_t> template <AccessEnum FieldAccess_e, typename Field_t> class Extendible<Derived_t>::FieldId
{
  friend Extendible; // allow direct access for internal classes

private:
  ExtendibleOffset_t const *offset_ptr = nullptr;

public:
  FieldId() {}
  bool
  isValid() const
  {
    return offset_ptr;
  }

private:
  FieldId(ExtendibleOffset_t const &offset) { offset_ptr = &offset; }
  ExtendibleOffset_t
  offset() const
  {
    return *offset_ptr;
  }
};

////////////////////////////////////////////////////
// Extendible::Schema Method Definitions
//

/// Add a new Field to this record type
template <typename Derived_t>
template <AccessEnum Access_t, typename Field_t>
bool
Extendible<Derived_t>::Schema::addField(FieldId<Access_t, Field_t> &field_id, std::string const &field_name)
{
  static_assert(Access_t == BIT || std::is_same<Field_t, bool>::value == false,
                "Use BitField so we can pack bits, they are still atomic.");
  ink_release_assert(instance_count == 0); // it's too late, we already started allocating.

  ExtendibleOffset_t size = 0;
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

  fields[field_name]  = FieldSchema{Access_t, std::type_index(typeid(Field_t)), size, 0, construct_fn, destruct_fn};
  field_id.offset_ptr = &fields[field_name].offset;
  updateMemOffsets();
  return true;
}

/// Add a new Field to this record type
template <typename Derived_t>
template <typename Field_t>
bool
Extendible<Derived_t>::Schema::addField(FieldId<ACIDPTR, Field_t> &field_id, std::string const &field_name)
{
  static_assert(std::is_copy_constructible<Field_t>::value == true, "Must have a copy constructor to use AcidPtr.");
  ink_release_assert(instance_count == 0); // it's too late, we already started allocating.
  using ptr_t             = AcidPtr<Field_t>;
  ExtendibleOffset_t size = sizeof(ptr_t);

  // capture the default constructors of the data type
  static auto construct_fn = [](void *ptr) { new (ptr) ptr_t(new Field_t()); };
  static auto destruct_fn  = [](void *ptr) { static_cast<ptr_t *>(ptr)->~ptr_t(); };

  fields[field_name]  = FieldSchema{ACIDPTR, std::type_index(typeid(Field_t)), size, 0, construct_fn, destruct_fn};
  field_id.offset_ptr = &fields[field_name].offset;
  updateMemOffsets();
  return true;
}

/// Add a new Field to this record type (for a C API)
template <typename Derived_t>
FieldId_C
Extendible<Derived_t>::Schema::addField_C(char const *field_name, size_t size, void (*construct_fn)(void *),
                                          void (*destruct_fn)(void *))
{
  ink_release_assert(size == 1 || size == 2 || size == 4 || size % 8 == 0); // must use aligned sizes
  ink_release_assert(instance_count == 0);                                  // it's too late, we already started allocating.
  const std::string field_name_str(field_name);
  fields[field_name_str] =
    FieldSchema{C_API, std::type_index(typeid(void *)), static_cast<ExtendibleOffset_t>(size), 0, construct_fn, destruct_fn};
  updateMemOffsets();
  return &fields[field_name].offset;
}

template <typename Derived_t>
template <AccessEnum Access_t, typename Field_t>
class Extendible<Derived_t>::FieldId<Access_t, Field_t>
Extendible<Derived_t>::Schema::find(std::string const &field_name)
{
  auto field_iter = fields.find(field_name);
  if (field_iter == fields.end()) {
    return Extendible<Derived_t>::FieldId<Access_t, Field_t>(); // didn't find name
  }
  FieldSchema &fs = field_iter->second;                    // found name
  ink_assert(fs.access == Access_t);                       // conflicting access, between field add and find
  ink_assert(fs.type == std::type_index(typeid(Field_t))); // conflicting type, between field add and find
  return Extendible<Derived_t>::FieldId<Access_t, Field_t>(fs.offset);
}

template <typename Derived_t>
FieldId_C
Extendible<Derived_t>::Schema::find_C(char const *field_name)
{
  auto field_iter = fields.find(field_name);
  ink_release_assert(field_iter != fields.end());
  FieldSchema &fs = field_iter->second;
  ink_release_assert(fs.access == C_API);
  return &fs.offset;
}

template <typename Derived_t>
void
Extendible<Derived_t>::Schema::updateMemOffsets()
{
  ink_release_assert(instance_count == 0);

  uint32_t acc_offset = ROUNDUP(sizeof(Derived_t), alloc_align);

  ExtendibleOffset_t size_blocks[] = {4, 2, 1};

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

template <typename Derived_t>
bool
Extendible<Derived_t>::Schema::reset()
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

template <typename Derived_t>
void
Extendible<Derived_t>::Schema::call_construct(char *ext_as_char_ptr)
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

template <typename Derived_t>
void
Extendible<Derived_t>::Schema::call_destruct(char *ext_as_char_ptr)
{
  for (auto const &elm : fields) {
    FieldSchema const &field_schema = elm.second;
    if (field_schema.access != BIT && field_schema.destruct_fn != nullptr) {
      field_schema.destruct_fn(ext_as_char_ptr + field_schema.offset);
    }
  }
  --instance_count;
}

template <typename Derived_t>
size_t
Extendible<Derived_t>::Schema::size() const
{
  return alloc_size;
}

template <typename Derived_t>
bool
Extendible<Derived_t>::Schema::no_instances() const
{
  return instance_count == 0;
}

////////////////////////////////////////////////////
// Extendible Method Definitions
//

/// return a reference to an atomic field (read, write or other atomic operation)
template <typename Derived_t>
template <typename Field_t>
std::atomic<Field_t> &
Extendible<Derived_t>::get(FieldId<ATOMIC, Field_t> const &field)
{
  return *at_offset<std::atomic<Field_t> *>(field.offset());
}

/// atomically read a bit value
template <typename Derived_t>
bool const
Extendible<Derived_t>::get(BitFieldId field) const
{
  return readBit(field);
}

/// atomically read a bit value
template <typename Derived_t>
bool const
Extendible<Derived_t>::readBit(BitFieldId field) const
{
  const char &c   = *(at_offset<char const *>(schema.bit_offset) + field.offset() / 8);
  const char mask = 1 << (field.offset() % 8);
  return (c & mask) != 0;
}

/// atomically write a bit value
template <typename Derived_t>
void
Extendible<Derived_t>::writeBit(BitFieldId field, bool const val)
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
template <typename Derived_t>
template <typename Field_t>
Field_t const & // value is not expected to change, or be freed while 'this' exists.
Extendible<Derived_t>::get(FieldId<STATIC, Field_t> field) const
{
  return *at_offset<const Field_t *>(field.offset());
}

/// return a reference to an static field that is non-const for initialization purposes
template <typename Derived_t>
template <typename Field_t>
Field_t &
Extendible<Derived_t>::init(FieldId<STATIC, Field_t> field)
{
  ink_release_assert(!areStaticsFrozen());
  return *at_offset<Field_t *>(field.offset());
}

/// return a shared pointer to last committed field value
template <typename Derived_t>
template <typename Field_t>
std::shared_ptr<const Field_t> // shared_ptr so the value can be updated while in use.
Extendible<Derived_t>::get(FieldId<ACIDPTR, Field_t> field) const
{
  const AcidPtr<Field_t> &reader = *at_offset<const AcidPtr<Field_t> *>(field.offset());
  return reader.getPtr();
}

/// return a writer created from the last committed field value
template <typename Derived_t>
template <typename Field_t>
AcidCommitPtr<Field_t>
Extendible<Derived_t>::writeAcidPtr(FieldId<ACIDPTR, Field_t> field)
{
  AcidPtr<Field_t> &reader = *at_offset<AcidPtr<Field_t> *>(field.offset());
  return AcidCommitPtr<Field_t>(reader);
}

/// return a reference to a field, without concurrent access protection
template <typename Derived_t>
template <typename Field_t>
Field_t & // value is not expected to change, or be freed while 'this' exists.
Extendible<Derived_t>::get(FieldId<DIRECT, Field_t> field)
{
  return *at_offset<Field_t *>(field.offset());
}

/// return a const reference to a field, without concurrent access protection
template <typename Derived_t>
template <typename Field_t>
Field_t const & // value is not expected to change, or be freed while 'this' exists.
Extendible<Derived_t>::get(FieldId<DIRECT, Field_t> field) const
{
  return *at_offset<const Field_t *>(field.offset());
}

/// C API
template <typename Derived_t>
void *
Extendible<Derived_t>::get(FieldId_C &field)
{
  return at_offset<void *>(*static_cast<ExtendibleOffset_t const *>(field));
}

/// allocate a new object with properties
template <class Derived_t>
void *
Extendible<Derived_t>::operator new(size_t size)
{
  // allocate one block for all the memory, including the derived_t members
  // return ::operator new(schema.alloc_size);
  void *ptr = ats_memalign(schema.alloc_align, schema.alloc_size);
  ink_release_assert(ptr != nullptr);
  return ptr;
}

// private
template <class Derived_t>
char *
Extendible<Derived_t>::this_as_char_ptr()
{
  return static_cast<char *>(static_cast<void *>(this));
}
// private
template <class Derived_t>
char const *
Extendible<Derived_t>::this_as_char_ptr() const
{
  return static_cast<char const *>(static_cast<void const *>(this));
}
// private
template <class Derived_t>
template <typename Return_t>
Return_t
Extendible<Derived_t>::at_offset(ExtendibleOffset_t offset)
{
  return reinterpret_cast<Return_t>(this_as_char_ptr() + offset);
}
// private
template <class Derived_t>
template <typename Return_t>
Return_t
Extendible<Derived_t>::at_offset(ExtendibleOffset_t offset) const
{
  return reinterpret_cast<Return_t>(this_as_char_ptr() + offset);
}

// TODO: override std::get<field_t>(Extendible &)

}; // namespace MT
