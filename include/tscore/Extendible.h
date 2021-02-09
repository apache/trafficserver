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

#pragma once
#include <cstdint>
#include <typeinfo>
#include <typeindex>
#include <cstddef>
#include <cstring>
#include <sstream>
#include <functional>
#include <iomanip>
#include <ostream>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <limits>

#include "tscore/AtomicBit.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_defs.h"

//////////////////////////////////////////
/// SUPPORT MACRO
#define DEF_EXT_NEW_DEL(cls)                                               \
  void *operator new(size_t sz) { return ats_malloc(ext::sizeOf<cls>()); } \
  void *operator new(size_t sz, void *ptr) { return ptr; }                 \
  void operator delete(void *ptr) { free(ptr); }

//////////////////////////////////////////
/// HELPER CLASSES

/////////////////////////
// Has 'const_iterator' trait class
template <typename T> class has_const_iterator
{
  template <typename U> static std::true_type check(typename U::const_iterator *);
  template <typename U> static std::false_type check(...);

public:
  static constexpr decltype(check<T>(nullptr)) value{}; // value is 'constexpr true' if T defines 'const_iterator'
};
/////////////////////////
// Has 'super_type' trait class
template <typename T> class has_super_type
{
  template <typename U> static std::true_type check(typename U::super_type *);
  template <typename U> static std::false_type check(...);

public:
  static constexpr decltype(check<T>(nullptr)) value{}; // value is 'constexpr true' if T defines 'super_type'
};

//////////////////////////////////////////
//////////////////////////////////////////
//////////////////////////////////////////

// C API

// context (internally FieldDesc*)
typedef void const *ExtFieldContext;
typedef void *DerivedPtr;
typedef void *FieldPtr;

FieldPtr ExtFieldPtr(DerivedPtr derived, ExtFieldContext field_context, int *size = nullptr);

//////////////////////////////////////////
//////////////////////////////////////////
//////////////////////////////////////////

namespace ext
{
////////////////////////////////////////////////////
// Forward declarations
template <typename Derived_t, typename Field_t> class FieldId; // field handle (strongly type)
template <typename Derived_t> class Extendible;

namespace details // internal stuff
{
  using Offest_t = uint16_t; // used to store byte offsets to fields
  struct FieldDesc;          // keeps the field properties, and methods
  class Schema;              // container of fields and methods

  // forward declare friend functions used outside of details
  template <typename T> uintptr_t initRecurseSuper(T &, uintptr_t);
  template <typename T> FieldPtr FieldPtrGet(Extendible<T> const &, details::FieldDesc const &);

  //////////////////////////////////////////

  bool &
  areFieldsFinalized()
  {
    static bool finalized = false;
    return finalized;
  }

  /////////////////////////////////////////////////////////////////////
  /// ext::details::FieldDesc - type erased field descriptor, with type specific std::functions
  struct FieldDesc {
    Offest_t ext_loc_offset;        ///< byte offset to Extendible._ext_loc
    Offest_t field_offset;          ///< byte offset from ext_loc to field
    std::type_index field_type_idx; ///< data type index
    uint16_t size;                  ///< byte size of field
    uint8_t align;                  ///< alignment of field
    uint8_t mask;                   ///< mask for packed bit operations

    // specialize the following
    std::function<void(FieldPtr)> constructor;
    std::function<void(FieldPtr)> destructor;
    std::function<void(std::ostream &, void const *)> serializer;

    FieldDesc() : field_type_idx(typeid(std::nullptr_t)) {}
  };

  /////////////////////////////////////////////////////////////////////
  /// ext::details::Schema manages the a static layout of fields as data structures
  class Schema
  {
  public:
    std::unordered_map<std::string, FieldDesc> fields; ///< defined elements of the blob by name
    size_t alloc_size                     = 0;         ///< bytes to allocate for fields
    uint8_t alloc_align                   = 1;         ///< alignment of block
    std::atomic<uint> cnt_constructed     = {0};       ///< the number of Extendible<Derived> created.
    std::atomic<uint> cnt_fld_constructed = {0};       ///< the number of Extendible<Derived> that constructed fields.
    std::atomic<uint> cnt_destructed      = {0};       ///< the number of Extendible<Derived> destroyed.

  public:
    Schema() {}
    ~Schema();

    /// Testing methods
    bool no_instances() const; ///< returns true if there are no instances of Extendible<Derived_t>
    bool reset();              ///< clears all field definitions.

    size_t fullSize(size_t base_size) const;       ///< returns sizeof memory allocated
    void updateMemOffsets();                       ///< updates memory offsets, alignment, and total allocation size
    void callConstructor(uintptr_t ext_start_ptr); ///< calls constructor for each field
    void callDestructor(uintptr_t ext_start_ptr);  ///< call destructor for each field

  }; // end Schema struct
} // namespace details

/// ext::Extendible allows code (and Plugins) to declare member-like variables during system init.
/*
 * This class uses a special allocator (ext::create) to extend the memory allocated to store run-time static
 * variables, which are registered by plugins during system init. The API is in a functional style to support
 * multiple inheritance of Extendible classes. This is templated so static variables are instanced per Derived
 * type, because we need to have different field schema per type.
 *
 * @tparam Derived_t - the class that you want to extend at runtime.
 *
 * @see test_Extendible.cc for examples
 *
 */
template <typename Derived_t> class Extendible
{
public:
  using short_ptr_t = uint16_t;

  // static
  static details::Schema schema; ///< one schema instance per Derived_t to define contained fields

  // return the address offset of the ext_loc member variable
  static constexpr size_t
  getLocOffset()
  {
    return offsetof(Extendible<Derived_t>, ext_loc);
  }

  // member variables
private:
  short_ptr_t ext_loc = 0; ///< byte offset to extendible storage

  // lifetime management
  /** don't allow copy construct, that doesn't allow atomicity */
public:
  Extendible(Extendible &) = delete;

protected:
  Extendible();
  // use ext::create() exclusively for allocation and initialization

  /** destruct all fields */
  ~Extendible();

private:
  /** construct all fields */
  size_t initFields(uintptr_t start_ptr); ///< tell this extendible where it's memory offset start is.

  uintptr_t
  getBegin() const
  {
    return uintptr_t(this) + ext_loc;
  }

  template <typename T> friend T *create();
  template <typename T> friend uintptr_t details::initRecurseSuper(T &, uintptr_t);
  template <typename T> friend FieldPtr details::FieldPtrGet(Extendible<T> const &, details::FieldDesc const &);
  template <typename T> friend std::string viewFormat(T const &, uintptr_t, int);
};

// define the static schema per derived type
template <typename Derived_t> details::Schema Extendible<Derived_t>::schema;

//####################################################
//####################################################
// UTILITY Functions

//////////////////////////////////////////////////////
/// HexToString function for serializing untyped C storage
// TODO: use ts::bwf::As_Hex() after PR goes through
inline void
hexToStream(std::ostream &os, void const *buf, uint16_t size)
{
  static const char hexDigits[] = "0123456789abcdef";

  const uint8_t *src = static_cast<const uint8_t *>(buf);
  for (int i = 0; i < size; i++) {
    os << hexDigits[src[i]];
  }
};

template <typename Field_t>
void
serializeField(std::ostream &os, Field_t const &f)
{
  using namespace std;
  // print containers as lists
  if constexpr (has_const_iterator<Field_t>::value) {
    os << "[";
    for (auto const &a : f) {
      serializeField(os, a);
      os << ", ";
    }
    os << "]";
    return;
  } else {
    os << f;
  }
}
//####################################################
//####################################################
//####################################################

/////////////////////////////////////////////////////////////////////
// FieldId
/// a strongly typed pointer to FieldDesc
template <typename Derived_t, typename Field_t> class FieldId
{
public:
  ext::details::FieldDesc const *desc = nullptr;
  bool isValid() const;
  FieldId(ext::details::FieldDesc const &);
  FieldId() {}
  FieldId(FieldId const &) = default;
};

namespace details
{
  template <typename Derived_t>
  FieldPtr
  FieldPtrGet(Extendible<Derived_t> const &d, FieldDesc const &desc)
  {
    return FieldPtr(d.Extendible<Derived_t>::getBegin() + desc.field_offset);
  }

  // overloadable function to construct and init an FieldId
  //////////////////////////////////////////////////////
  /// Type Generic Template

  template <typename Derived_t, typename Field_t>
  Field_t const &
  fieldGet(void const *fld_ptr, FieldId<Derived_t, Field_t> const &field)
  {
    return *static_cast<Field_t const *>(fld_ptr);
  }

  template <typename Derived_t, typename Field_t>
  Field_t &
  fieldSet(void *fld_ptr, FieldId<Derived_t, Field_t> const &field)
  {
    return *static_cast<Field_t *>(fld_ptr);
  }

  template <typename Derived_t, typename Field_t>
  void
  makeFieldId(FieldId<Derived_t, Field_t> &id, FieldDesc &desc)
  {
    ink_assert(!areFieldsFinalized());

    desc.field_type_idx = std::type_index(typeid(Field_t));
    desc.ext_loc_offset = Extendible<Derived_t>::getLocOffset();
    desc.field_offset   = std::numeric_limits<decltype(desc.field_offset)>::max();
    desc.size           = sizeof(Field_t);
    desc.align          = alignof(Field_t);
    desc.mask           = 0;

    id = FieldId<Derived_t, Field_t>(desc);

    //
    desc.constructor = [](FieldPtr fld_ptr) { new (fld_ptr) Field_t(); };
    desc.destructor  = [](FieldPtr fld_ptr) { static_cast<Field_t *>(fld_ptr)->~Field_t(); };
    desc.serializer  = [id](std::ostream &os, void const *fld_ptr) { serializeField(os, fieldGet(fld_ptr, id)); };
  }

  //////////////////////////////////////////////////////
  /// C API specialization
  // no type or constructor. Just a size.

  template <typename Derived_t>
  void
  makeFieldId(FieldDesc &desc, uint16_t size)
  {
    ink_assert(!areFieldsFinalized());
    desc.field_type_idx = typeid(void);
    desc.ext_loc_offset = Extendible<Derived_t>::getLocOffset();
    desc.field_offset   = std::numeric_limits<decltype(desc.field_offset)>::max();
    desc.size           = size;
    desc.align          = 1;
    desc.mask           = 0;

    //
    desc.constructor = nullptr;
    desc.destructor  = nullptr;
    desc.serializer  = [size](std::ostream &os, void const *fld_ptr) { hexToStream(os, fld_ptr, size); };
  }

  //////////////////////////////////////////////////////
  /// Bool specializations

  template <typename Derived_t>
  const bool
  fieldGet(const void *fld_ptr, FieldId<Derived_t, bool> const &field)
  {
    return bool((*static_cast<const uint8_t *>(fld_ptr)) & field.desc->mask);
  }

  template <typename Derived_t>
  AtomicBit
  fieldSet(FieldPtr fld_ptr, FieldId<Derived_t, bool> const &field)
  {
    return AtomicBit{static_cast<uint8_t *>(fld_ptr), field.desc->mask};
  }

  template <typename Derived_t>
  void
  makeFieldId(FieldId<Derived_t, bool> &id, FieldDesc &desc)
  {
    desc.field_type_idx = std::type_index(typeid(bool));
    desc.ext_loc_offset = Extendible<Derived_t>::getLocOffset();
    desc.field_offset   = std::numeric_limits<decltype(desc.field_offset)>::max();
    desc.size           = 0;
    desc.align          = 0;
    desc.mask           = 0;

    id = FieldId<Derived_t, bool>(desc);

    //
    desc.constructor = nullptr;
    desc.destructor  = nullptr;
    desc.serializer  = [id](std::ostream &os, void const *fld_ptr) { serializeField(os, fieldGet(fld_ptr, id)); };
  }

  //////////////////////////////////////////////////////
  /// std::atomic<bool> specializations (same as bool)

  template <typename Derived_t>
  inline const bool
  fieldGet(void const *fld_ptr, FieldId<Derived_t, std::atomic<bool>> const &field)
  {
    return bool(fld_ptr & field.mask);
  }

  template <typename Derived_t>
  inline AtomicBit
  fieldSet(FieldPtr fld_ptr, FieldId<Derived_t, std::atomic<bool>> const &field)
  {
    return AtomicBit{fld_ptr, field.mask};
  }

  template <typename Derived_t>
  void
  makeFieldId(FieldId<Derived_t, std::atomic<bool>> &id, FieldDesc &desc)
  {
    desc.field_type_idx = std::type_index(typeid(std::atomic<bool>));
    desc.ext_loc_offset = Extendible<Derived_t>::getLocOffset();
    desc.field_offset   = std::numeric_limits<decltype(desc.field_offset)>::max();
    desc.size           = 0;
    desc.align          = 0;
    desc.mask           = 0;

    id = FieldId<Derived_t, std::atomic<bool>>(desc);

    //
    desc.constructor = nullptr;
    desc.destructor  = nullptr;
    desc.serializer  = [id](std::ostream &os, void const *fld_ptr) { serializeField(os, fieldGet(fld_ptr, id)); };
  }
} // namespace details

//////////////////////////////////////////////////////
/// safely cast FieldDesc back to FieldId

template <typename Derived_t, typename Field_t>
FieldId<Derived_t, Field_t>::FieldId(ext::details::FieldDesc const &fld_desc) : desc(&fld_desc)
{
  const size_t loc = Extendible<Derived_t>::getLocOffset();
  ink_assert(loc == fld_desc.ext_loc_offset);
  ink_assert(std::type_index(typeid(Field_t)) == fld_desc.field_type_idx);
}

template <typename Derived_t, typename Field_t>
bool
FieldId<Derived_t, Field_t>::isValid() const
{
  return desc != nullptr;
}

//####################################################
//####################################################
//####################################################
// Functional API for Extendible Field Access
//

////////////////////////////////////////////////////
// Schema Method Definitions
//

/// Add a new Field to this record type
template <class Derived_t, class Field_t>
bool
fieldAdd(FieldId<Derived_t, Field_t> &field_id, char const *field_name)
{
  using namespace ext::details;
  Schema &schema = Extendible<Derived_t>::schema;
  ink_release_assert(schema.no_instances()); // it's too late, we already started allocating.
  ink_release_assert(!areFieldsFinalized()); // it's too late, Fields must be added during Plugin Init.

  auto field_iter = schema.fields.find(field_name);
  if (field_iter != schema.fields.end()) {
    return false;
  }

  makeFieldId(field_id, schema.fields[field_name]);
  schema.updateMemOffsets();
  return true;
}

/// Add a new Field to Derived_t, this C function uses a fat API
template <class Derived_t>
ExtFieldContext
fieldAdd(char const *field_name, int size, void (*construct_fn)(void *), void (*destruct_fn)(void *))
{
  using namespace ext::details;
  Schema &schema = Extendible<Derived_t>::schema;
  ink_release_assert(schema.no_instances()); // it's too late, we already started allocating.
  ink_release_assert(!areFieldsFinalized()); // it's too late, Fields must be added during Plugin Init.
  ink_release_assert(size >= 0);             // non-negative numbers please

  auto field_iter = schema.fields.find(field_name);
  if (field_iter != schema.fields.end()) {
    return nullptr;
  }
  if (size == 0) {
    FieldId<Derived_t, bool> id;
    makeFieldId(id, schema.fields[field_name]);
  } else {
    FieldDesc &desc = schema.fields[field_name];
    makeFieldId<Derived_t>(desc, size);
    desc.constructor = construct_fn;
    desc.destructor  = destruct_fn;
  }
  schema.updateMemOffsets();
  return &schema.fields[field_name];
}

template <class Derived_t, class Field_t>
bool
fieldFind(FieldId<Derived_t, Field_t> &field_id, char const *field_name)
{
  using namespace ext::details;
  ink_release_assert(areFieldsFinalized());
  Schema const &schema = Extendible<Derived_t>::schema;
  auto field_iter      = schema.fields.find(field_name);
  if (field_iter == schema.fields.end()) {
    return false; // didn't find name
  }
  field_id = FieldId<Derived_t, Field_t>(field_iter->second);
  return true;
}

// each Derived_t will have a DerivedExtfieldFind
template <class Derived_t>
ExtFieldContext
fieldFind(char const *field_name)
{
  using namespace ext::details;
  ink_release_assert(areFieldsFinalized());
  Schema const &schema = Extendible<Derived_t>::schema;
  auto field_iter      = schema.fields.find(field_name);
  if (field_iter == schema.fields.end()) {
    return nullptr; // didn't find name
  }
  return &field_iter->second;
}

////////////////////////////////////////////////////
/// ext::get & ext::set accessor functions
template <typename T, typename Derived_t, typename Field_t>
inline decltype(auto)
get(T const &d, FieldId<Derived_t, Field_t> &field)
{
  Extendible<Derived_t> const &ext = d;
  FieldPtr fld_ptr                 = ext::details::FieldPtrGet(ext, *field.desc);
  return ext::details::fieldGet(fld_ptr, field);
}

template <typename T, typename Derived_t, typename Field_t>
inline decltype(auto)
set(T &d, FieldId<Derived_t, Field_t> &field)
{
  Extendible<Derived_t> const &ext = d;
  FieldPtr fld_ptr                 = ext::details::FieldPtrGet(ext, *field.desc);
  return ext::details::fieldSet(fld_ptr, field);
}

/////////////////////////
// ext::sizeOf - returns the size of a class + all extensions.
//

template <typename Derived_t>
inline size_t
sizeOf(size_t size = sizeof(Derived_t))
{
  // add size of super extendibles
  if constexpr (has_super_type<Derived_t>::value) {
    size = ext::sizeOf<typename Derived_t::super_type>(size);
  } else {
    static_assert(std::is_same<decltype(Derived_t::schema), decltype(Extendible<Derived_t>::schema)>::value,
                  "ambiguous schema, Derived_t is missing super_type");
  }

  // add size of this extendible
  if constexpr (std::is_base_of<Extendible<Derived_t>, Derived_t>::value) {
    size = Extendible<Derived_t>::schema.fullSize(size);
    // assert that the schema is not ambiguous.
  }
  return size;
}

/////////////////////////
// Ext Alloc & Init
//
template <class Derived_t> Extendible<Derived_t>::Extendible()
{
  ink_assert(ext::details::areFieldsFinalized());
  // don't call callConstructor until the derived class is fully constructed.
  ++schema.cnt_constructed;
}

template <class Derived_t> Extendible<Derived_t>::~Extendible()
{
  // assert callConstructors was called.
  ink_assert(ext_loc);
  schema.callDestructor(uintptr_t(this) + ext_loc);
  ++schema.cnt_destructed;
  ink_assert(schema.cnt_destructed <= schema.cnt_fld_constructed);
}

/// tell this extendible where it's memory offset start is. Added to support inheriting from extendible classes
template <class Derived_t>
uintptr_t
Extendible<Derived_t>::initFields(uintptr_t start_ptr)
{
  ink_assert(ext_loc == 0);
  start_ptr = ROUNDUP(start_ptr, schema.alloc_align); // pad the previous struct, so that our fields are memaligned correctly
  ink_assert(start_ptr - uintptr_t(this) < UINT16_MAX);
  ext_loc = uint16_t(start_ptr - uintptr_t(this)); // store the offset to be used by ext::get and ext::set
  ink_assert(ext_loc > 0);
  schema.callConstructor(start_ptr);    // construct all fields
  return start_ptr + schema.alloc_size; // return the end of the extendible data
}

namespace details
{
  /// recursively init all extendible structures, and construct fields
  template <typename Derived_t>
  uintptr_t
  initRecurseSuper(Derived_t &devired, uintptr_t tail_ptr /*= 0*/)
  {
    // track a tail pointer, that starts after the class, and interate each extendible block
    if constexpr (has_super_type<Derived_t>::value) {
      // init super type, move tail pointer
      tail_ptr = initRecurseSuper<typename Derived_t::super_type>(devired, tail_ptr);
    }
    if constexpr (std::is_base_of<Extendible<Derived_t>, Derived_t>::value) {
      // set start for this extendible block after the previous extendible, and move tail pointer to after this block
      tail_ptr = devired.Extendible<Derived_t>::initFields(tail_ptr);
    }
    return tail_ptr;
  }

} // namespace details

// allocate and initialize an extendible data structure
template <typename Derived_t, typename... Args>
Derived_t *
create(Args &&... args)
{
  // don't instantiate until all Fields are finalized.
  ink_assert(ext::details::areFieldsFinalized());

  // calculate the memory needed for the class and all Extendible blocks
  const size_t type_size = ext::sizeOf<Derived_t>();

  // alloc one block of memory
  Derived_t *ptr = static_cast<Derived_t *>(ats_memalign(alignof(Derived_t), type_size));

  // construct (recursively super-to-sub class)
  new (ptr) Derived_t(std::forward(args)...);

  // define extendible blocks start offsets (recursively super-to-sub class)
  details::initRecurseSuper(*ptr, uintptr_t(ptr) + sizeof(Derived_t));
  return ptr;
}

/////////////////////////
// ExtDebugFormat - print a ascii chart of memory layout of a class
//
//  Example layout of C -> B -> A, where C and A are extendible. All contain 1 int.
//  See test_Extendible.cc for class implementation.
//  1A | EXT  | 2b | ##________##__
//  1A | BASE | 2b | __##__________
//  1B | BASE | 2b | ____##________
//  1C | EXT  | 2b | ______##____##
//  1C | BASE | 2b | ________##____

template <typename T>
std::string
viewFormat(T const &t, uintptr_t _base_addr = 0, int _full_size = ext::sizeOf<T>())
{
  using namespace std;
  stringstream ss;
  if (_base_addr == 0) {
    _base_addr = uintptr_t(&t);
  }
  int super_size = 0;
  if constexpr (has_super_type<T>::value) {
    ss << viewFormat<typename T::super_type>(t, _base_addr, _full_size);
    super_size += sizeof(typename T::super_type);
  }

  if constexpr (is_base_of<Extendible<T>, T>::value) {
    Extendible<T> const *e = &t;
    int ptr_start          = uintptr_t(e) + Extendible<T>::getLocOffset() - _base_addr;
    int ptr_end            = ptr_start + sizeof(typename Extendible<T>::short_ptr_t);
    int ext_start          = e->getBegin() - _base_addr;
    int ext_end            = Extendible<T>::schema.fullSize(ext_start);

    ink_assert(ptr_end <= ext_start);
    ink_assert(ext_end <= _full_size);

    ss << endl << setw(30) << typeid(T).name() << " | EXT  | " << setw(5) << ext_end - ext_start << "b |";
    ss << string(ptr_start, '_').c_str();
    ss << string(ptr_end - ptr_start, '#').c_str();
    ss << string(ext_start - ptr_end, '_').c_str();
    ss << string(ext_end - ext_start, '#').c_str();
    ss << string(_full_size - ext_end, '_').c_str();

    super_size += sizeof(Extendible<T>);
  }

  int super_start  = uintptr_t(&t) - _base_addr;
  int member_start = super_start + super_size;
  int member_end   = super_start + sizeof(T);

  ink_assert(member_start <= member_end);
  ink_assert(member_end <= _full_size);

  ss << endl << setw(30) << typeid(T).name() << " | BASE | " << setw(5) << sizeof(T) - super_size << "b |";
  ss << string(super_start, '_').c_str();
  ss << string(member_start - super_start, '_').c_str();
  ss << string(member_end - member_start, '#').c_str();
  ss << string(_full_size - member_end, '_').c_str();

  return ss.str();
}

namespace details
{
  std::string
  ltrim(std::string const &str, const std::string &chars = "\t\n\v\f\r ")
  {
    std::string r(str);
    r.erase(0, str.find_first_not_of(chars));
    return r;
  }
} // namespace details

template <typename T>
void
serialize(std::ostream &os, T const &t)
{
  using namespace std;
  size_t indent = os.width();
  os << endl << setw(indent) << "" << details::ltrim(typeid(T).name(), " 0123456789") << ": {" << endl;
  indent += 2;
  if constexpr (is_base_of<Extendible<T>, T>::value) {
    if constexpr (has_super_type<T>::value) {
      serialize<typename T::super_type>(os, t);
    }
    auto const &schema = T::schema;
    size_t name_width  = 0;
    for (const auto &kv : schema.fields) {
      name_width = max(name_width, kv.first.length());
    }
    // TODO: clang-5 didn't like the use of a range based for here, change later
    for (auto it = schema.fields.begin(); it != schema.fields.end(); ++it) {
      auto &fname = it->first;
      auto &field = it->second;
      ink_assert(field.serializer);
      os << setw(indent) << "" << setw(name_width) << right << fname << ": ";
      field.serializer(os, details::FieldPtrGet(t, field));
      os << "," << endl;
    }
  }
  indent -= 2;
  os << setw(indent + 1) << "}";
}

template <typename T>
std::string
toString(T const &t)
{
  std::stringstream ss;
  ss.width(0);
  serialize(ss, t);
  return ss.str();
}
} // namespace ext

// C API
//

FieldPtr
ExtFieldPtr(DerivedPtr derived, ExtFieldContext field_context, int *size /*= nullptr*/)
{
  using namespace ext;
  using namespace ext::details;
  ink_assert(field_context);
  ink_assert(derived);
  FieldDesc const &desc = *static_cast<FieldDesc const *>(field_context);
  if (size) {
    *size = desc.size;
  }

  Offest_t const *loc = (Offest_t const *)(uintptr_t(derived) + desc.ext_loc_offset);
  return FieldPtr(uintptr_t(derived) + (*loc) + desc.field_offset);
}
