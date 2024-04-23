/** @file
    Wrapper class for crypto hashes.

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

#include "tsutil/ts_bw_format.h"
#include "tscore/ink_memory.h"
#include <openssl/evp.h>
#include <string_view>

/// Apache Traffic Server commons.

#if TS_ENABLE_FIPS == 1
#define CRYPTO_HASH_SIZE (256 / 8)
#else
#define CRYPTO_HASH_SIZE (128 / 8)
#endif
#define CRYPTO_HEX_SIZE ((CRYPTO_HASH_SIZE * 2) + 1)

namespace ts
{
/// Crypto hash output.
union CryptoHash {
  uint64_t b[CRYPTO_HASH_SIZE / sizeof(uint64_t)]; // Legacy placeholder
  uint64_t u64[CRYPTO_HASH_SIZE / sizeof(uint64_t)];
  uint32_t u32[CRYPTO_HASH_SIZE / sizeof(uint32_t)];
  uint8_t  u8[CRYPTO_HASH_SIZE / sizeof(uint8_t)];

  /// Default constructor - init to zero.
  CryptoHash() { memset(this, 0, sizeof(*this)); }
  /// Copy constructor.
  CryptoHash(CryptoHash const &that) = default;

  /// Assignment - bitwise copy.
  CryptoHash &
  operator=(CryptoHash const &that)
  {
    if (this != &that) {
      memcpy(this, &that, sizeof(*this));
    }
    return *this;
  }

  /// Equality - bitwise identical.
  bool
  operator==(CryptoHash const &that) const
  {
    return ::memcmp(this, &that, sizeof(*this)) == 0;
  }

  /// Equality - bitwise identical.
  bool
  operator!=(CryptoHash const &that) const
  {
    return !(*this == that);
  }

  /// Reduce to 64 bit value.
  uint64_t
  fold() const
  {
#if CRYPTO_HASH_SIZE == 16
    return u64[0] ^ u64[1];
#elif CRYPTO_HASH_SIZE == 32
    return u64[0] ^ u64[1] ^ u64[2] ^ u64[3];
#endif
  }

  /// Access 64 bit slice.
  uint64_t
  operator[](int i) const
  {
    return u64[i];
  }

  /// Access 64 bit slice.
  /// @note Identical to @ operator[] but included for symmetry.
  uint64_t
  slice64(int i) const
  {
    return u64[i];
  }

  /// Access 32 bit slice.
  uint32_t
  slice32(int i) const
  {
    return u32[i];
  }

  /// Fast conversion to hex in fixed sized string.
  char *toHexStr(char buffer[(CRYPTO_HASH_SIZE * 2) + 1]) const;

  bool
  is_zero() const
  {
    constexpr uint8_t z64[sizeof(u64)] = {0};
    return ::memcmp(u64, z64, sizeof(z64)) == 0;
  }

  void
  clear()
  {
    ink_zero(u64);
  }
};

class CryptoContext
{
public:
  /** Protocol class for a crypto hash context.

     A hash of this type is used for strong hashing, such as for URLs.
   */
  class Hasher
  {
    using self_type = Hasher; ///< Self reference type.
  public:
    /// Destructor (force virtual)
    virtual ~Hasher() {}
    /// Update the hash with @a data of @a length bytes.
    virtual bool update(void const *data, int length) = 0;
    /// Finalize and extract the @a hash.
    virtual bool finalize(CryptoHash &hash) = 0;

    /// Convenience overload.
    bool finalize(CryptoHash *hash);

  protected:
    EVP_MD_CTX *_ctx = nullptr;
  };

  CryptoContext();

  /// Update the hash with @a data of @a length bytes.
  bool update(void const *data, int length);

  /// Convenience - compute final @a hash for @a data.
  /// @note This is just as fast as the previous style, as a new context must be initialized
  /// every time this is done.
  bool hash_immediate(CryptoHash &hash, void const *data, int length);

  /// Finalize and extract the @a hash.
  bool finalize(CryptoHash &hash);

  enum HashType {
    UNSPECIFIED,
#if TS_ENABLE_FIPS == 0
    MD5,
#endif
    SHA256,
  }; ///< What type of hash we really are.
  static HashType Setting;

  ~CryptoContext();

private:
  static size_t constexpr OBJ_SIZE = 256;
  char _base[OBJ_SIZE];
};

inline bool
CryptoContext::Hasher::finalize(CryptoHash *hash)
{
  return this->finalize(*hash);
}

inline bool
CryptoContext::update(void const *data, int length)
{
  return reinterpret_cast<Hasher *>(_base)->update(data, length);
}

inline bool
CryptoContext::finalize(CryptoHash &hash)
{
  return reinterpret_cast<Hasher *>(_base)->finalize(hash);
}

inline bool
CryptoContext::hash_immediate(CryptoHash &hash, void const *data, int length)
{
  return this->update(data, length) && this->finalize(hash);
}

inline CryptoContext::~CryptoContext()
{
  std::destroy_at(reinterpret_cast<Hasher *>(_base));
}

swoc::BufferWriter &bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, ts::CryptoHash const &hash);

} // namespace ts

using ts::CryptoContext;
using ts::CryptoHash;
