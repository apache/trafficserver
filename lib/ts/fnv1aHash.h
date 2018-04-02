/** @file

    Definitions to allow the creation of std::unordered_map-compatible FNV-1a hash functons for user-defined types.

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

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function

#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>

#include <ts/Series.h>

namespace ts
{
namespace _private_
{
  template <typename Result, Result Prime, Result OffsetBasis> class Fnv1aAccum
  {
  public:
    using Element = std::uint8_t;

    void
    operator()(std::uint8_t elem)
    {
      _hash ^= elem;
      _hash *= Prime;
    }

    Result
    result() const
    {
      return _hash;
    }

  private:
    Result _hash = OffsetBasis;
  };

  template <typename Result, Result Prime, Result OffsetBasis, typename Obj>
  Result
  fnv1aHash(const Obj &obj)
  {
    using Accum = Fnv1aAccum<Result, Prime, OffsetBasis>;

    Accum a;

    Series<Accum, Obj>::visit(a, obj);

    return a.result();
  }

} // end namespace _private_

// This will instantiate properly for a type Obj as long as ts::Series<Accum, Obj> instantiates.
//
template <typename Obj>
std::uint32_t
fnv1aHash32(const Obj &obj)
{
  using U                 = uint32_t;
  constexpr U Prime       = (static_cast<U>(1) << 24) bitor (static_cast<U>(1) << 8) bitor 0x93;
  constexpr U BasisOffset = 0x811c9dc5;

  return _private_::fnv1aHash<U, Prime, BasisOffset, Obj>(obj);
}

// This will instantiate properly for a type Obj as long as ts::Series<Accum, Obj, std::uint64_t, std::uint8_t> instantiates.
//
template <typename Obj>
std::uint64_t
fnv1aHash64(const Obj &obj)
{
  using U                 = uint64_t;
  constexpr U Prime       = (static_cast<U>(1) << 40) bitor (static_cast<U>(1) << 8) bitor 0x3b;
  constexpr U BasisOffset = (static_cast<U>(0xcbf29ce4) << 32) bitor 0x84222325;

  return _private_::fnv1aHash<U, Prime, BasisOffset, Obj>(obj);
}

namespace _private_
{
  template <typename Obj, int bits> struct FuncFnv1aHash;

  template <typename Obj> struct FuncFnv1aHash<Obj, 32> {
    static std::size_t
    x(const Obj &obj)
    {
      return fnv1aHash32(obj);
    }
  };

  template <typename Obj> struct FuncFnv1aHash<Obj, 64> {
    static std::size_t
    x(const Obj &obj)
    {
      return fnv1aHash64(obj);
    }
  };

} // end namespace _private_

// std::unordered_map-compatible hash function (as long as std::size_t is either uint32_t or uint64_t).
//
// It will instantiate properly for a type Obj as long as ts::Series<Accum, Obj, std::size_t, std::uint8_t> instantiates.
//
template <typename Obj>
std::size_t
fnv1aHash(const Obj &obj)
{
  return _private_::FuncFnv1aHash<Obj, std::numeric_limits<std::size_t>::digits>::x(obj);
}

// NOTE: This facility is better than std::hash, because it is more easily extensible to user-defined classes.

} // end namespace ts
