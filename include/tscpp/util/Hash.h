/** @file Basic hash function support.

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
#include "tscpp/util/MemSpan.h"
#include "tscpp/util/TextView.h"

namespace ts
{
/** Base protocol class for hash functors.
 *
 * Each specific hash function embedded in a hash functor is a subclass of this class and
 * follows this API. Subclasses should override the return type to return the subclass type.
 *
 * The main purpose of this is to allow run time changes in hashing, which is required in various
 * circumstances.
 */
struct HashFunctor {
  using self_type = HashFunctor; ///< Self reference type.

  /// Pass @a data to the hashing function.
  virtual self_type &update(std::string_view const &data) = 0;

  /// Finalize the hash function output.
  virtual self_type & final() = 0;

  /// Reset the hash function state.
  virtual self_type &clear() = 0;

  /// Get the size of the resulting hash value.
  virtual size_t size() const = 0;

  /// Copy the result to @a dst.
  /// @a dst must be at least @c result_size bytes long.
  /// @return @c true if the result was copied to @a data, @c false otherwise.
  virtual bool get(MemSpan dst) const = 0;

  virtual ~HashFunctor() = default; ///< Force virtual destructor.
};

/// A hash function that returns a 32 bit result.
struct Hash32Functor : HashFunctor {
protected:
  using self_type = Hash32Functor;

public:
  using value_type = uint32_t;

  // Co-vary the return type.
  self_type &update(std::string_view const &data) override = 0;

  self_type & final() override = 0;

  self_type &clear() override = 0;

  virtual value_type get() const = 0;

  /// Get the size of the resulting hash value.
  size_t size() const override;

  bool get(MemSpan dst) const override;

  /** Immediately produce a hash value from @a data.
   *
   * @param data Hash input
   * @return Hash value.
   *
   * This is a convenience method for when all the data to hash is already available.
   */
  value_type hash_immediate(std::string_view const &data);
};

struct Hash64Functor : HashFunctor {
protected:
  using self_type = Hash64Functor;

public:
  using value_type = uint64_t;

  // Co-vary the return type.
  self_type &update(std::string_view const &data) override = 0;

  self_type & final() override = 0;

  self_type &clear() override = 0;

  virtual value_type get() const = 0;

  /// Get the size of the resulting hash value.
  size_t size() const override;

  bool get(MemSpan dst) const override;

  /** Immediately produce a hash value from @a data.
   *
   * @param data hash input
   * @return Hash value.
   *
   * This is a convenience method for when all the data to hash is already available.
   */
  value_type hash_immediate(std::string_view const &data);
};

// ----
// Implementation

inline auto
Hash32Functor::hash_immediate(std::string_view const &data) -> value_type
{
  return this->update(data).final().get();
}

inline size_t
Hash32Functor::size() const
{
  return sizeof(value_type);
}

inline auto
Hash64Functor::hash_immediate(std::string_view const &data) -> value_type
{
  return this->update(data).final().get();
}

inline size_t
Hash64Functor::size() const
{
  return sizeof(value_type);
}

} // namespace ts
