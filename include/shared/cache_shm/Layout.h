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

#include "tscore/ink_align.h"
#include "tscore/ink_memory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace cache_shm
{

constexpr char             CACHE_SHM_MAGIC[8]       = {'A', 'T', 'S', '-', 'S', 'H', 'M', '\0'};
constexpr uint32_t         CACHE_SHM_SCHEMA_VERSION = 1;
constexpr std::string_view CACHE_SHM_CONTROL        = "control";

// macOS PSHMNAMLEN is 31 chars including the leading '/'. Keep names under that
// limit on Linux too, so the same naming works everywhere.
constexpr std::size_t MAX_SHM_NAME_LEN = 31;

// Maximum number of stripes in the control segment. Bumping it changes both the ABI
// hash and sizeof(CacheShmControl); a prior segment is dropped on attach either way
// (see CONTROL_HEADER_SIZE for how the size change is detected).
constexpr std::size_t MAX_STRIPES = 256;

// Per-stripe entry in the control segment. A stripe is matched to its prior
// segment on attach by stripe_key_hash, not by name (order-independent).
// dir_untrusted lives here and not in the stripe's own header because that header aliases raw_dir, which is also the source
// buffer for the on-disk directory write -- a mark written there can reach disk and cost the stripe on the next start.
struct StripeEntry {
  char     shm_name[MAX_SHM_NAME_LEN + 1]; ///< full shm name, NUL-terminated.
  uint64_t raw_dir_size;                   ///< size of the stripe's raw_dir segment, bytes.
  uint64_t stripe_key_hash;                ///< full 64-bit FNV-1a of the stripe hash_text.
  uint8_t  dir_untrusted;                  ///< 1 = shutdown could not vouch for this directory; never attach it again.
  uint8_t  pad0[7];
};

struct CacheShmControl {
  char     magic[8];       ///< CACHE_SHM_MAGIC
  uint32_t schema_version; ///< CACHE_SHM_SCHEMA_VERSION
  uint32_t pad0;
  uint64_t abi_hash;          ///< compile-time ABI fingerprint
  uint64_t storage_signature; ///< storage.config fingerprint
  uint8_t  clean_shutdown;    ///< 0 = dirty, 1 = clean
  uint8_t  pad1[3];
  int32_t  owner_pid; ///< PID of the process that took the segment; 0 when none. Backs the
                      ///< concurrent-attach guard, so it is held until the owner exits, not
                      ///< cleared at clean shutdown; the next start tests it for liveness.
  uint32_t    stripe_count;
  uint32_t    pad2;
  StripeEntry stripes[MAX_STRIPES];
};

constexpr std::size_t CONTROL_SIZE = sizeof(CacheShmControl);

// FROZEN: append to StripeEntry or grow stripes[] freely, but never reorder or extend the bytes ahead of stripes[]. A
// build with a different sizeof(CacheShmControl) must still be able to read this far to drop the segment rather than
// wedge on EEXIST forever. See "The frozen control header" in the shm-fast-restart developer guide.
constexpr std::size_t CONTROL_HEADER_SIZE = offsetof(CacheShmControl, stripes);
static_assert(CONTROL_HEADER_SIZE == 48, "the control segment header is a frozen layout; see the comment above");
static_assert(std::is_standard_layout_v<CacheShmControl>, "the control segment is shared across processes and builds");

// Whether a control segment of `actual` bytes was written by *this* build; the kernel rounds an shm object up to a page.
// Anything larger has a stripes[] of unknown stride and must never be walked with our layout. Shared by the attach gate,
// the purge primitive and `traffic_ctl cache shm status` so the three cannot drift apart.
inline bool
is_own_control_size(std::size_t actual)
{
  return actual >= CONTROL_SIZE && actual <= INK_ALIGN(CONTROL_SIZE, ats_pagesize());
}

// Frame the operator's middle word (e.g. "ats") as "/<word>-". The framing is supplied here so it cannot be mis-typed:
// stray framing from an older config is trimmed, and embedded '/' stripped since POSIX permits only the leading one.
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

// Name of the "<prefix>control" segment. Derived here so the cache subsystem and
// traffic_ctl agree; `prefix` is the normalized prefix (e.g. "/ats-").
inline std::string
control_segment_name(std::string_view prefix)
{
  return std::string(prefix) + CACHE_SHM_CONTROL.data();
}

} // namespace cache_shm
