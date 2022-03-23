/** @file

  xxHash support class.

  https://github.com/Cyan4973/xxHash

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

#if TS_HAS_XXHASH

#include "tscore/CryptoHash.h"

#include <xxhash.h>

/**
   This is NOT cryptographic hash
 */
class xxHashContext
{
public:
  /**
     Dest type is CryptoHash for compatibility for a historical reason.

     TODO: get rid of CryptoHash.
   */
  static bool
  hash_immediate(CryptoHash &hash, void const *data, int length)
  {
    XXH128_hash_t r = XXH3_128bits(data, length);
    hash            = reinterpret_cast<CryptoHash const &>(r);

    return true;
  }

  xxHashContext() { _state = XXH3_createState(); }

  ~xxHashContext() { XXH3_freeState(_state); }

  /// Update the hash with @a data of @a length bytes.
  bool
  update(void const *data, int length)
  {
    if (XXH3_128bits_update(_state, data, length) == XXH_ERROR) {
      return false;
    }

    return true;
  }

  /// Finalize and extract the @a hash.
  bool
  finalize(CryptoHash &hash)
  {
    XXH128_hash_t r = XXH3_128bits_digest(_state);
    hash            = reinterpret_cast<CryptoHash const &>(r);

    return true;
  }

private:
  XXH3_state_t *_state = nullptr;
};

#endif
