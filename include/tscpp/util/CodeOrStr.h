/* Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#pragma once

#include <string_view>
#include <cstring>

#if !defined(UtilAssert)
#if __has_include(<tscore/ink_assert.h>)
#include <tscore/ink_assert.h>
#define UtilAssert ink_assert
#else // Must be in plugin.
#include <ts/ts.h>
#define UtilAssert TSAssert
#endif
#endif // !defined(UtilAssert)

#if !defined(TS_1ST_OF_2)
#define TS_1ST_OF_2(P1, P2) P1
#endif

#if !defined(TS_2P_COMMA_ADD_1)
#define TS_2P_COMMA_ADD_1(P1, P2) ts::CommaAdd(1)
#endif

#if !defined(TS_2ND_OF_2)
#define TS_2ND_OF_2(P1, P2) P2
#endif

/*
A macro to generate a struct that allows for creating an enum where each enum value is associated with a
string constant, with support for conversion between a value of the enum and it's corresponding string.

The NAME parameter is a identifier that will be the name of the struct.

The M parameter is a macro that specifies the enum values and the correspnding strings.  It must have the form:

#define L(X) \
X(EnumValA, "String for A"), \
X(EnumValB, "String for B"), \
...
X(EnumValWithoutString, ), \ <-- The string value will be std::string_view{}
...
X(EnumValY, "String for Y"), \
X(EnumValZ, "String for Z")

This macro can be undefined after TS_CVT_CODE_STR_N() is invoked.

The NORMALIZE_CHAR parameter an identifier such that the expression NORMALIZE_CHAR(c), c a char, returns a
char.

The type member Code is the class enum.

NumCode is the number of enum values.

str() returns the string for an enum value.

idx_to_str() returns the string for the numeric equivalent of an enum value.

to_idx(string_view) returns the index which is the numeric equivalent of the enum value corresponding to the
passed string.  Each character of the passed string will be normalized using NORMALIZE_CHAR, before searching
for it in the list of strings.  If the string does not correspond to any enum value, to_idx() returns -1.
*/
#define TS_CVT_CODE_STR_N(NAME, M, NORMALIZE_CHAR)                      \
  struct NAME {                                                         \
    enum class Code { M(TS_1ST_OF_2) };                                 \
    static std::size_t const NumCodes = (M(TS_2P_COMMA_ADD_1)).value(); \
    static std::string_view                                             \
    idx_to_str(std::size_t idx)                                         \
    {                                                                   \
      static std::string_view const s[] = {M(TS_2ND_OF_2)};             \
      UtilAssert(idx < NumCodes);                                       \
      return s[idx];                                                    \
    }                                                                   \
    static std::string_view                                             \
    str(Code c)                                                         \
    {                                                                   \
      return idx_to_str(static_cast<std::size_t>(c));                   \
    }                                                                   \
    static int                                                          \
    to_idx(std::string_view sv)                                         \
    {                                                                   \
      return ts::sv_lookup(idx_to_str, NumCodes, sv, NORMALIZE_CHAR);   \
    }                                                                   \
  };

#define TS_CVT_CODE_STR(NAME, M) TS_CVT_CODE_STR_N(NAME, M, ts::tosame)

#if !defined(TS_SAME)
#define TS_SAME(P) P
#endif

#if !defined(TS_1P_COMMA_ADD)
#define TS_1P_COMMA_ADD(P1) ts::CommaAdd(1)
#endif

#if !defined(TS_MAKE_STR)
#define TS_MAKE_STR_(S) #S
#define TS_MAKE_STR(S) TS_MAKE_STR_(S)
#endif

/*
A macro to generate a struct that allows for creating an enum where each enum value is associated with a
string constant that is the enum value name, with support for conversion between a value of the enum and
it's corresponding string constant.

The NAME parameter is a identifier that will be the name of the struct.

The M parameter is a macro that specifies the enum values and the correspnding strings.  It must have the form:

#define L(X) \
X(EnumValA), \
X(EnumValB), \
...
X(EnumValY), \
X(EnumValZ)

This macro can be undefined after TS_CVT_CODE_N() is invoked.

The NORMALIZE_CHAR parameter an identifier such that the expression NORMALIZE_CHAR(c), c a char, returns a
char.

The type member Code is the class enum.

NumCode is the number of enum values.

str() returns the string for an enum value.

idx_to_str() returns the string for the numeric equivalent of an enum value.

to_idx(string_view) returns the index wich is the numeric equivalent of the enum value corresponding to the
passed string.  Each character of the passed string will be normalized using NORMALIZE_CHAR, before searching
for it in the list of strings.  If the string does not correspond to any enum value, to_idx() returns -1.
*/
#define TS_CVT_CODE_N(NAME, M, NORMALIZE_CHAR)                        \
  struct NAME {                                                       \
    enum class Code { M(TS_SAME) };                                   \
    static std::size_t const NumCodes = (M(TS_1P_COMMA_ADD)).value(); \
    static std::string_view                                           \
    idx_to_str(std::size_t idx)                                       \
    {                                                                 \
      static std::string_view const s[] = {M(TS_MAKE_STR)};           \
      UtilAssert(idx < NumCodes);                                     \
      return s[idx];                                                  \
    }                                                                 \
    static std::string_view                                           \
    str(Code c)                                                       \
    {                                                                 \
      return idx_to_str(static_cast<std::size_t>(c));                 \
    }                                                                 \
    static int                                                        \
    to_idx(std::string_view sv)                                       \
    {                                                                 \
      return ts::sv_lookup(idx_to_str, NumCodes, sv, NORMALIZE_CHAR); \
    }                                                                 \
  };

#define TS_CVT_CODE(NAME, M) TS_CVT_CODE_N(NAME, M, ts::tosame)

namespace ts
{
inline char
tosame(char c)
{
  return c;
}

/*
A function to look up a string in an list of strings.

lu_func is a function/functor that takes an index into the list of string and returns the string.

lu_dimension is the number of (0-base) indexes.

value is the string to look up in the list.  Each character of value is converted before it is looked up in the
list.

cvt_func is the function/functor that takes a char as its parameter and returns the converted character.
*/
template <typename LuFunc, typename CharCvt>
int
sv_lookup(LuFunc lu_func, std::size_t lu_dimension, std::string_view value, CharCvt cvt_func = tosame)
{
  for (std::size_t i = 0; i < lu_dimension; ++i) {
    std::string_view sv = lu_func(i);
    if (value.size() == sv.size()) {
      for (std::size_t j = 0;; ++j) {
        if (value.size() == j) {
          return i;
        }
        if (cvt_func(value[j]) != sv[j]) {
          break;
        }
      }
    }
  }
  return -1;
}

class CommaAdd
{
public:
  explicit constexpr CommaAdd(int i) : _i(i) {}
  constexpr CommaAdd operator , (CommaAdd const &rhs) const
  {
    return CommaAdd(_i + rhs._i);
  }
  constexpr int
  value() const
  {
    return _i;
  }

private:
  int _i;
};

// A class that can contain either an unsigned number or a string_view.  If its value is a string_view, it
// can optionally own the string_view data if it is dynamically allocated.
//
class UnsOrStr
{
public:
  // Create an instance containing an unsigned value.
  //
  explicit UnsOrStr(unsigned v = 0) : _u{v | _IS_UNS} { UtilAssert((v & ~_VAL) == 0); }

  // Create an instance containing a string_view without owning the string_view data.
  //
  static UnsOrStr ref(std::string_view);

  // Copy another instance.  If the other instance contains a string_view, the returned instance will not own
  // its data.
  //
  static UnsOrStr ref(UnsOrStr const &);

  // Create an instance containing a string_view, copying the input string_view's data into an owned allocaeted
  // array.
  //
  static UnsOrStr dup(std::string_view);

  // Create another instance.  If the other instance contains a string_view, its data is copied into an owned
  // allocaeted array.
  //
  static UnsOrStr dup(UnsOrStr const &);

  // Create an instance containing a string_view, taking ownership of the string_view data (which must be
  // dynamically allocated).
  //
  static UnsOrStr own(std::string_view);

  UnsOrStr(UnsOrStr &&);
  UnsOrStr &operator=(UnsOrStr &&);

  UnsOrStr(UnsOrStr const &) = delete;
  UnsOrStr &operator=(UnsOrStr const &) = delete;

  // Returns true if instance ontains an unsigned value (otherwise it contains a string).
  //
  bool
  is_uns() const
  {
    return (_u & _IS_UNS) != 0;
  }

  // Contained value if it is an unsigned value.
  //
  unsigned
  uns() const
  {
    UtilAssert(is_uns());
    return _u & _VAL;
  }

  // Contained value if it is a string.
  //
  std::string_view
  str() const
  {
    UtilAssert(!is_uns());
    return std::string_view(_s, _u & _VAL);
  }

  ~UnsOrStr()
  {
    if (_u & _IS_OWNED) {
      delete[] _s;
    }
  }

  friend bool operator==(UnsOrStr const &a, UnsOrStr const &b);

private:
  static unsigned const _VAL      = ~0U >> 2;            // Mask of bits other than the 2 most significant.
  static unsigned const _IS_OWNED = (~0U >> 1) ^ _VAL;   // Mask of 2nd most significant bit.
  static unsigned const _IS_UNS   = ~(_IS_OWNED | _VAL); // Mast most significant bit.

  // Contains _IS_UNS and _IS_OWNED flags.  Remaining bits are either unsigned value or size of string value.
  //
  unsigned _u;

// Pointer to string data if value is a string.
//
#if defined(__clang_analyzer__)
  char const *_s = nullptr;
#else
  char const *_s;
#endif
};

#if !defined(__clang_analyzer__)

inline UnsOrStr
UnsOrStr::ref(std::string_view sv)
{
  UtilAssert((sv.size() & ~_VAL) == 0);

  UnsOrStr ret;

  ret._u = sv.size();
  ret._s = sv.data();

  return ret;
}

inline UnsOrStr
UnsOrStr::ref(UnsOrStr const &src)
{
  UnsOrStr ret;

  if (src.is_uns()) {
    ret._u = src._u;

  } else {
    ret = ref(src.str());
  }

  return ret;
}

inline UnsOrStr
UnsOrStr::dup(std::string_view sv)
{
  UtilAssert((sv.size() & ~_VAL) == 0);

  UnsOrStr ret;

  ret._u = sv.size();

  if (!sv.size()) {
    ret._s = nullptr;

  } else {
    auto p = new char[sv.size()];
    std::memcpy(p, sv.data(), sv.size());
    ret._s = p;
    ret._u |= _IS_OWNED;
  }

  return ret;
}

inline UnsOrStr
UnsOrStr::dup(UnsOrStr const &src)
{
  UnsOrStr ret;

  if (src.is_uns()) {
    ret._u = src._u;

  } else {
    ret = dup(src.str());
  }

  return ret;
}

inline UnsOrStr
UnsOrStr::own(std::string_view sv)
{
  UnsOrStr ret = ref(sv);

  if (sv.size()) {
    ret._u |= _IS_OWNED;
  }

  return ret;
}

#else

// Clang Analyzer did not get the memo that return value optimization is no longer optional in C++17.

inline UnsOrStr UnsOrStr::ref(std::string_view)
{
  return UnsOrStr{};
}

inline UnsOrStr
UnsOrStr::ref(UnsOrStr const &)
{
  return UnsOrStr{};
}

inline UnsOrStr UnsOrStr::dup(std::string_view)
{
  return UnsOrStr{};
}

inline UnsOrStr
UnsOrStr::dup(UnsOrStr const &)
{
  return UnsOrStr{};
}

inline UnsOrStr UnsOrStr::own(std::string_view)
{
  return UnsOrStr{};
}

#endif

inline UnsOrStr::UnsOrStr(UnsOrStr &&src)
{
  _u = src._u;
  _s = src._s;

  src._u &= ~_IS_OWNED;
}

inline UnsOrStr &
UnsOrStr::operator=(UnsOrStr &&src)
{
  this->~UnsOrStr();

  _u = src._u;
  _s = src._s;

  src._u &= ~_IS_OWNED;

  return *this;
}

inline bool
operator==(UnsOrStr const &a, UnsOrStr const &b)
{
  if (((a._u ^ b._u) & ~UnsOrStr::_IS_OWNED) != 0) {
    return false;
  }
  if (a._u & UnsOrStr::_IS_UNS) {
    return true;
  }
  return std::memcmp(a._s, b._s, a._u & UnsOrStr::_VAL) == 0;
}

inline bool
operator!=(UnsOrStr const &a, UnsOrStr const &b)
{
  return !(a == b);
}

// A class to contain either the value of the enum Cvt::Code or some string value.  Cvt must be a class created
// with a TS_CVT_CODE_xxx macro, or have an equivalent interface.  Can optionally own the string data if it
// is dynamically allocated.
//
template <class Cvt> class CodeOrStr : private UnsOrStr
{
public:
  using Code = typename Cvt::Code;

  // Create an instance containing an enum value.
  //
  explicit CodeOrStr(Code v = Code()) : UnsOrStr(static_cast<unsigned>(v)) {}

  // Create an instance.  If the string converts to an enum value, that will be the value of the instance.
  // Otherwise, the instance will have the value of the string (which the caller must guarantee has a lifetime
  // longer than any CodeOrStr intstances refering to it).
  //
  static CodeOrStr ref(std::string_view);

  // Copy an instance, not owning any string data (which the caller must guarantee has a lifetime longer than
  // any CodeOrStr intstances refering to it).
  //
  static CodeOrStr ref(CodeOrStr const &src);

  // Create an instance.  If the string converts to an enum value, that will be the value of the instance.
  // Otherwise, copy the string data into an owned dynamically allocated array.  The instance will have the
  // value of the string.
  //
  static CodeOrStr dup(std::string_view);

  // Copy an instance.  If src contains a string, copy the string data into an owned dynamically allocated
  // array.
  //
  static CodeOrStr dup(CodeOrStr const &src);

  // Returns true if the contained value is an enum value, otherwise it's a string.
  //
  bool
  is_code() const
  {
    return UnsOrStr::is_uns();
  }

  Code
  code() const
  {
    UtilAssert(is_code());
    return static_cast<Code>(UnsOrStr::uns());
  }

  std::string_view
  str() const
  {
    if (is_code()) {
      return Cvt::idx_to_str(UnsOrStr::uns());
    }
    return UnsOrStr::str();
  }

  friend bool
  operator==(CodeOrStr const &a, CodeOrStr const &b)
  {
    return a.base() == b.base();
  }

  friend bool
  operator!=(CodeOrStr const &a, CodeOrStr const &b)
  {
    return !(a == b);
  }

  UnsOrStr const &
  base() const
  {
    return *this;
  }
};

#if !defined(__clang_analyzer__)

template <class Cvt>
CodeOrStr<Cvt>
CodeOrStr<Cvt>::ref(std::string_view sv)
{
  int idx = Cvt::to_idx(sv);

  if (idx >= 0) {
    return CodeOrStr<Cvt>(static_cast<Code>(idx));
  }
  return static_cast<CodeOrStr<Cvt> &&>(UnsOrStr::ref(sv));
}

template <class Cvt>
CodeOrStr<Cvt>
CodeOrStr<Cvt>::ref(CodeOrStr const &src)
{
  return static_cast<CodeOrStr &&>(UnsOrStr::ref(src));
}

template <class Cvt>
CodeOrStr<Cvt>
CodeOrStr<Cvt>::dup(std::string_view sv)
{
  int idx = Cvt::to_idx(sv);

  if (idx >= 0) {
    return CodeOrStr<Cvt>(static_cast<Code>(idx));
  }
  char *p = nullptr;
  if (sv.size()) {
    p = new char[sv.size()];
    std::memcpy(p, sv.data(), sv.size());
  }
  return static_cast<CodeOrStr<Cvt> &&>(UnsOrStr::own(std::string_view(p, sv.size())));
}

template <class Cvt>
CodeOrStr<Cvt>
CodeOrStr<Cvt>::dup(CodeOrStr const &src)
{
  return static_cast<CodeOrStr &&>(UnsOrStr::ref(src));
}

#else

// Clang Analyzer did not get the memo that return value optimization is no longer optional in C++17.

template <class Cvt> CodeOrStr<Cvt> CodeOrStr<Cvt>::ref(std::string_view)
{
  return CodeOrStr<Cvt>{};
}

template <class Cvt>
CodeOrStr<Cvt>
CodeOrStr<Cvt>::ref(CodeOrStr const &src)
{
  return CodeOrStr<Cvt>{};
}

template <class Cvt> CodeOrStr<Cvt> CodeOrStr<Cvt>::dup(std::string_view)
{
  return CodeOrStr<Cvt>{};
}

template <class Cvt>
CodeOrStr<Cvt>
CodeOrStr<Cvt>::dup(CodeOrStr const &)
{
  return CodeOrStr<Cvt>{};
}

#endif

} // end namespace ts
