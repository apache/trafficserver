/** @file

  Layout of the cache shared-memory control segment, shared between the cache
  subsystem and tools (traffic_ctl) that inspect or clear the segment without
  going through the running traffic_server.

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
#include <string>
#include <string_view>

namespace cache_shm
{

constexpr char             CACHE_SHM_MAGIC[8]       = {'A', 'T', 'S', '-', 'S', 'H', 'M', '\0'};
constexpr uint32_t         CACHE_SHM_SCHEMA_VERSION = 1;
constexpr std::string_view CACHE_SHM_CONTROL        = "control";

// macOS PSHMNAMLEN is 31 chars including the leading '/'. Keep names under that
// limit on Linux too, so the same naming works everywhere.
constexpr std::size_t MAX_SHM_NAME_LEN = 31;

// Maximum number of stripes in the control segment. Bumping it changes the ABI
// hash, so old segments are dropped automatically.
constexpr std::size_t MAX_STRIPES = 256;

// Per-stripe entry in the control segment. A stripe is matched to its prior
// segment on attach by stripe_key_hash, not by name (order-independent).
struct StripeEntry {
  char     shm_name[MAX_SHM_NAME_LEN + 1]; ///< full shm name, NUL-terminated.
  uint64_t raw_dir_size;                   ///< size of the stripe's raw_dir segment, bytes.
  uint64_t stripe_key_hash;                ///< full 64-bit FNV-1a of the stripe hash_text.
};

struct CacheShmControl {
  char     magic[8];       ///< CACHE_SHM_MAGIC
  uint32_t schema_version; ///< CACHE_SHM_SCHEMA_VERSION
  uint32_t pad0;
  uint64_t abi_hash;          ///< compile-time ABI fingerprint
  uint64_t storage_signature; ///< storage.yaml fingerprint
  uint8_t  clean_shutdown;    ///< 0 = dirty, 1 = clean
  uint8_t  pad1[3];
  int32_t  owner_pid; ///< PID holding the exclusive lock; 0 when none. Backs the
                      ///< concurrent-attach guard. Cleared on clean shutdown.
  uint32_t    stripe_count;
  uint32_t    pad2;
  StripeEntry stripes[MAX_STRIPES];
};

constexpr std::size_t CONTROL_SIZE = sizeof(CacheShmControl);

// Normalize the operator-configured prefix into the full shm name prefix used
// to build segment names. The operator sets only the middle word (e.g. "ats");
// the framing is supplied here so the leading '/' that POSIX shm_open requires
// and the '-' separating the prefix from the per-object suffix can never be
// mis-typed. Any stray framing carried over from an older config (e.g. a
// literal "/ats-") is trimmed first, so migration can never yield an invalid
// embedded-slash name like "//ats--". An embedded '-' in the middle is preserved;
// an embedded '/' is stripped, since POSIX shm names permit only the leading '/'
// (a mistyped "foo/bar" would otherwise build a name shm_open rejects with EINVAL).
// Both the running server and traffic_ctl normalize through here so they agree on
// the same names.
inline std::string
normalize_name_prefix(std::string_view configured)
{
  std::size_t begin = configured.find_first_not_of('/');
  if (begin == std::string_view::npos) {
    begin = configured.size(); // all '/' (or empty): no middle.
  }
  std::size_t      last_kept = configured.find_last_not_of('-');
  std::string_view middle    = (last_kept == std::string_view::npos || last_kept < begin) ?
                                 std::string_view{} :
                                 configured.substr(begin, last_kept - begin + 1);
  std::string      word{"/"};
  for (char c : middle) {
    if (c != '/') { // POSIX shm names allow only the leading '/'.
      word += c;
    }
  }
  word += "-";
  return word;
}

// Name of the "<prefix>control" segment. Derived in one place so the cache
// subsystem and traffic_ctl agree on it. `prefix` is the normalized prefix
// (see normalize_name_prefix), e.g. "/ats-".
inline std::string
control_segment_name(std::string_view prefix)
{
  return std::string(prefix) + CACHE_SHM_CONTROL.data();
}

} // namespace cache_shm
