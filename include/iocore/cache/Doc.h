/** @file

  A brief file description

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

#include "tscore/CryptoHash.h"

#include <cstdint>

#define DOC_MAGIC       ((uint32_t)0x5F129B13)
#define DOC_CORRUPT     ((uint32_t)0xDEADBABE)
#define DOC_NO_CHECKSUM ((uint32_t)0xA0B0C0D0)

// Note : hdr() needs to be 8 byte aligned.
struct Doc {
  uint32_t magic;     // DOC_MAGIC
  uint32_t len;       // length of this fragment (including hlen & sizeof(Doc), unrounded)
  uint64_t total_len; // total length of document
#if TS_ENABLE_FIPS == 1
  // For FIPS CryptoHash is 256 bits vs. 128, and the 'first_key' must be checked first, so
  // ensure that the new 'first_key' overlaps the old 'first_key' and that the rest of the data layout
  // is the same by putting 'key' at the ned.
  CryptoHash first_key; ///< first key in object.
#else
  CryptoHash first_key; ///< first key in object.
  CryptoHash key;       ///< Key for this doc.
#endif
  uint32_t hlen;         ///< Length of this header.
  uint32_t doc_type : 8; ///< Doc type - indicates the format of this structure and its content.
  uint32_t v_major  : 8; ///< Major version number.
  uint32_t v_minor  : 8; ///< Minor version number.
  uint32_t unused   : 8; ///< Unused, forced to zero.
  uint32_t sync_serial;
  uint32_t write_serial;
  uint32_t pinned; ///< pinned until - CAVEAT: use uint32_t instead of time_t for the cache compatibility
  uint32_t checksum;
#if TS_ENABLE_FIPS == 1
  CryptoHash key; ///< Key for this doc.
#endif

  uint32_t data_len() const;
  uint32_t prefix_len() const;
  int single_fragment() const;
  char *hdr();
  char *data();
};

inline uint32_t
Doc::prefix_len() const
{
  return sizeof(Doc) + hlen;
}

inline uint32_t
Doc::data_len() const
{
  return len - sizeof(Doc) - hlen;
}

inline int
Doc::single_fragment() const
{
  return data_len() == total_len;
}

inline char *
Doc::hdr()
{
  return reinterpret_cast<char *>(this) + sizeof(Doc);
}

inline char *
Doc::data()
{
  return this->hdr() + hlen;
}
