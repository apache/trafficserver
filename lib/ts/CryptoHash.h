/** @file
    Protocol class for crypto hashes.

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

#if !defined CRYPTO_HASH_HEADER
#define CRYPTO_HASH_HEADER

/// Apache Traffic Server commons.

#include "ts/ink_code.h"

namespace ats
{
/// Crypto hash output.
union CryptoHash {
  uint64_t b[2]; // Legacy placeholder
  uint64_t u64[2];
  uint32_t u32[4];
  uint8_t u8[16];

  /// Default constructor - init to zero.
  CryptoHash()
  {
    u64[0] = 0;
    u64[1] = 0;
  }

  /// Assignment - bitwise copy.
  CryptoHash &
  operator=(CryptoHash const &that)
  {
    u64[0] = that.u64[0];
    u64[1] = that.u64[1];
    return *this;
  }

  /// Equality - bitwise identical.
  bool
  operator==(CryptoHash const &that) const
  {
    return u64[0] == that.u64[0] && u64[1] == that.u64[1];
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
    return u64[0] ^ u64[1];
  }

  /// Access 64 bit slice.
  uint64_t operator[](int i) const { return u64[i]; }
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
  char *
  toHexStr(char buffer[33])
  {
    return ink_code_to_hex_str(buffer, u8);
  }
};

extern CryptoHash const CRYPTO_HASH_ZERO;

/** Protocol class for a crypto hash context.

    A hash of this type is used for strong hashing, such as for URLs.
*/
class CryptoContext
{
  typedef CryptoContext self; ///< Self reference type.
public:
  /// Destructor (force virtual)
  virtual ~CryptoContext() {}
  /// Update the hash with @a data of @a length bytes.
  virtual bool update(void const *data, int length) = 0;
  /// Finalize and extract the @a hash.
  virtual bool finalize(CryptoHash &hash) = 0;

  /// Convenience overload.
  bool finalize(CryptoHash *hash);

  /// Convenience - compute final @a hash for @a data.
  /// @note This is just as fast as the previous style, as a new context must be initialized
  /// everytime this is done.
  virtual bool hash_immediate(CryptoHash &hash, void const *data, int length);
};

inline bool
CryptoContext::hash_immediate(CryptoHash &hash, void const *data, int length)
{
  return this->update(data, length) && this->finalize(hash);
}
inline bool
CryptoContext::finalize(CryptoHash *hash)
{
  return this->finalize(*hash);
}

} // end namespace

// Promote for the primitives who don't use namespaces...
using ats::CryptoHash;
using ats::CryptoContext;
using ats::CRYPTO_HASH_ZERO;

#endif // CRYPTO_HASH_HEADER
