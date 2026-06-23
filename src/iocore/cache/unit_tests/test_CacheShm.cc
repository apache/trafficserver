/** @file

  Unit tests for the cache shared-memory trust gates and control-segment layout.

  These exercise the pure, side-effect-free pieces of the shm fast-restart
  feature -- the ABI fingerprint, the storage signature, the control-header
  layout, and the macOS shm-name-length constraint -- without standing up a
  cache. They are the logic that decides whether a prior shm segment may be
  attached (fast restart) or must be dropped and rebuilt from disk.

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

#include "main.h"

#include "../CacheShm.h"
#include "../CacheShmLayout.h"

#include "iocore/cache/Store.h"
#include "tscore/ink_memory.h"

#include <cstring>
#include <limits>

#include <unistd.h>

// Required by the shared test harness (main.cc).
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

// Build a single-span Store with the given path and size (in STORE_BLOCK_SIZE
// blocks). The returned Store owns the Span and frees it on destruction.
void
make_store(Store &store, const char *path, int64_t blocks, int64_t offset = 0)
{
  store.extend(1);
  auto *span          = new Span();
  span->pathname      = ats_strdup(path);
  span->blocks        = blocks;
  span->offset        = offset;
  span->file_pathname = true;
  store.spans[0]      = span;
}

} // namespace

TEST_CASE("CacheShm ABI hash is stable and non-zero", "[cache][shm]")
{
  const uint64_t a = CacheShm::abi_hash();
  const uint64_t b = CacheShm::abi_hash();

  // Deterministic: the fingerprint is a pure function of compile-time layout.
  CHECK(a == b);
  // A zero hash would defeat the trust gate (every segment would look matching);
  // the FNV-1a seed and the struct sizes guarantee it is non-zero.
  CHECK(a != 0);
}

TEST_CASE("CacheShm storage signature is sensitive to topology", "[cache][shm]")
{
  Store base;
  make_store(base, "/cache/disk0", 1000);

  SECTION("identical topology -> identical signature")
  {
    Store same;
    make_store(same, "/cache/disk0", 1000);
    CHECK(CacheShm::storage_signature(base) == CacheShm::storage_signature(same));
  }

  SECTION("different path -> different signature")
  {
    Store other;
    make_store(other, "/cache/disk1", 1000);
    CHECK(CacheShm::storage_signature(base) != CacheShm::storage_signature(other));
  }

  SECTION("different size -> different signature")
  {
    Store resized;
    make_store(resized, "/cache/disk0", 2000);
    CHECK(CacheShm::storage_signature(base) != CacheShm::storage_signature(resized));
  }

  SECTION("different offset -> different signature")
  {
    Store moved;
    make_store(moved, "/cache/disk0", 1000, /*offset=*/512);
    CHECK(CacheShm::storage_signature(base) != CacheShm::storage_signature(moved));
  }

  SECTION("an empty store has a stable signature")
  {
    Store empty0;
    Store empty1;
    CHECK(CacheShm::storage_signature(empty0) == CacheShm::storage_signature(empty1));
  }
}

TEST_CASE("CacheShm control header round-trips through a byte buffer", "[cache][shm]")
{
  using cache_shm::CACHE_SHM_MAGIC;
  using cache_shm::CACHE_SHM_SCHEMA_VERSION;
  using cache_shm::CacheShmControl;
  using cache_shm::CONTROL_SIZE;

  // The on-shm size must equal the struct size; tooling (traffic_ctl) maps
  // exactly CONTROL_SIZE bytes and reads the struct out of it.
  CHECK(CONTROL_SIZE == sizeof(CacheShmControl));

  CacheShmControl src;
  std::memset(&src, 0, sizeof(src));
  std::memcpy(src.magic, CACHE_SHM_MAGIC, sizeof(CACHE_SHM_MAGIC));
  src.schema_version    = CACHE_SHM_SCHEMA_VERSION;
  src.abi_hash          = 0x0123456789abcdefULL;
  src.storage_signature = 0xfedcba9876543210ULL;
  src.clean_shutdown    = 1;
  src.owner_pid         = 4242;
  src.stripe_count      = 2;
  std::strncpy(src.stripes[0].shm_name, "/ats-s0", sizeof(src.stripes[0].shm_name) - 1);
  src.stripes[0].raw_dir_size    = 4096;
  src.stripes[0].stripe_key_hash = 0xaaaabbbbccccddddULL;
  std::strncpy(src.stripes[1].shm_name, "/ats-s1", sizeof(src.stripes[1].shm_name) - 1);
  src.stripes[1].raw_dir_size    = 8192;
  src.stripes[1].stripe_key_hash = 0x1111222233334444ULL;

  // Serialize to a raw byte buffer and read it back, mimicking shm attach.
  unsigned char buf[CONTROL_SIZE];
  std::memcpy(buf, &src, CONTROL_SIZE);
  const auto *dst = reinterpret_cast<const CacheShmControl *>(buf);

  CHECK(std::memcmp(dst->magic, CACHE_SHM_MAGIC, sizeof(CACHE_SHM_MAGIC)) == 0);
  CHECK(dst->schema_version == CACHE_SHM_SCHEMA_VERSION);
  CHECK(dst->abi_hash == 0x0123456789abcdefULL);
  CHECK(dst->storage_signature == 0xfedcba9876543210ULL);
  CHECK(dst->clean_shutdown == 1);
  CHECK(dst->owner_pid == 4242);
  CHECK(dst->stripe_count == 2);
  CHECK(std::string(dst->stripes[0].shm_name) == "/ats-s0");
  CHECK(dst->stripes[0].raw_dir_size == 4096);
  CHECK(dst->stripes[0].stripe_key_hash == 0xaaaabbbbccccddddULL);
  CHECK(std::string(dst->stripes[1].shm_name) == "/ats-s1");
  CHECK(dst->stripes[1].raw_dir_size == 8192);
  CHECK(dst->stripes[1].stripe_key_hash == 0x1111222233334444ULL);
}

TEST_CASE("CacheShm names respect the macOS PSHMNAMLEN limit", "[cache][shm]")
{
  using cache_shm::MAX_SHM_NAME_LEN;
  using cache_shm::StripeEntry;

  // macOS caps POSIX shm names at 31 chars including the leading '/'. The shared
  // limit must match so the same naming works on Linux and macOS alike.
  CHECK(MAX_SHM_NAME_LEN == 31);

  // The per-stripe name field must hold a maximum-length name plus its NUL.
  CHECK(sizeof(StripeEntry{}.shm_name) > MAX_SHM_NAME_LEN);

  // The default control segment name fits comfortably under the limit.
  const std::string control_name = cache_shm::control_segment_name("/ats-");
  CHECK(control_name.size() < MAX_SHM_NAME_LEN);
}

TEST_CASE("CacheShm normalizes the configured name prefix", "[cache][shm]")
{
  using cache_shm::normalize_name_prefix;

  // The operator configures only the middle word; the framing '/' and '-' are
  // supplied by the code so a name like "/ats-" cannot be mis-typed.
  CHECK(normalize_name_prefix("ats") == "/ats-");
  CHECK(normalize_name_prefix("foo") == "/foo-");

  // Forgiving of stray framing an operator may carry over (e.g. a pre-existing
  // "/ats-" config), so migration cannot produce "//ats--".
  CHECK(normalize_name_prefix("/ats-") == "/ats-");
  CHECK(normalize_name_prefix("/ats") == "/ats-");
  CHECK(normalize_name_prefix("ats-") == "/ats-");
  CHECK(normalize_name_prefix("//ats--") == "/ats-");

  // An embedded '/' or '-' in the middle is preserved -- only the framing is
  // trimmed.
  CHECK(normalize_name_prefix("ats-v2") == "/ats-v2-");
}

TEST_CASE("CacheShm process liveness check backs the concurrent-attach guard", "[cache][shm]")
{
  // Our own PID is, by definition, live -- this is the "a different live owner
  // still holds the segment" case the guard refuses to attach over.
  CHECK(CacheShm::process_is_alive(static_cast<int>(getpid())));

  // A zero / negative owner_pid means "no owner recorded" (e.g. after a clean
  // shutdown); it must never read as live or the guard would wrongly refuse.
  CHECK_FALSE(CacheShm::process_is_alive(0));
  CHECK_FALSE(CacheShm::process_is_alive(-1));

  // A PID at the top of the range is overwhelmingly unlikely to name a live
  // process; kill(pid, 0) returns ESRCH, so it reads as not-alive (a stale
  // owner left by a crash, which the guard is free to reclaim).
  CHECK_FALSE(CacheShm::process_is_alive(std::numeric_limits<int>::max()));
}
