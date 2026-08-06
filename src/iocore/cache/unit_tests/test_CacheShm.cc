/** @file

  Unit tests for the cache shared-memory trust gates and control-segment layout: the logic deciding whether a prior shm
  segment may be attached or must be dropped and rebuilt from disk.

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
#include "shared/cache_shm/Layout.h"
#include "shared/cache_shm/Purge.h"

#include "iocore/cache/Store.h"
#include "tscore/ink_config.h"
#include "tscore/ink_memory.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Required by the shared test harness (main.cc).
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

// The returned Store owns the Span and frees it on destruction.
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

  // An embedded '-' in the middle is preserved -- only the framing is trimmed.
  CHECK(normalize_name_prefix("ats-v2") == "/ats-v2-");

  // An embedded '/' is stripped: POSIX shm names permit only the leading '/', so a
  // mistyped middle word must not build a name shm_open would reject with EINVAL.
  CHECK(normalize_name_prefix("foo/bar") == "/foobar-");
  CHECK(normalize_name_prefix("/ats/v2/") == "/atsv2-");
  CHECK(normalize_name_prefix("a/b/c") == "/abc-");
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

// The rest of this file needs real shm objects, unlike the layout/fingerprint cases
// above, so it is gated the same way the feature is.
#if TS_USE_CACHE_SHM

namespace
{

// A prefix of our own so these tests can never touch a real instance's segments.
constexpr const char *PURGE_PREFIX_WORD = "atspurgetest";

// Valid magic, one claimed stripe, and `owner_pid` as given. `size` may be short of CONTROL_SIZE -- an older build with a
// smaller stripe table -- so only what exists is mapped. False if shm is unavailable here.
bool
plant_control_segment(const std::string &prefix, std::size_t size, int32_t owner_pid = 0)
{
  const std::string name = cache_shm::control_segment_name(prefix);
  shm_unlink(name.c_str());
  int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    return false;
  }
  if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
    close(fd);
    shm_unlink(name.c_str());
    return false;
  }
  const std::size_t map_len = std::min(size, cache_shm::CONTROL_SIZE);
  void             *addr    = mmap(nullptr, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (addr == MAP_FAILED) {
    shm_unlink(name.c_str());
    return false;
  }
  auto *ctrl = static_cast<cache_shm::CacheShmControl *>(addr);
  std::memset(ctrl, 0, map_len);
  std::memcpy(ctrl->magic, cache_shm::CACHE_SHM_MAGIC, sizeof(cache_shm::CACHE_SHM_MAGIC));
  ctrl->schema_version = cache_shm::CACHE_SHM_SCHEMA_VERSION;
  ctrl->owner_pid      = owner_pid;
  ctrl->stripe_count   = 1;
  // Deliberately unnamed: a foreign build's stripes[] may have another stride, so a correct purge cannot read this.
  munmap(addr, map_len);
  return true;
}

bool
plant_stripe_segment(const std::string &name)
{
  shm_unlink(name.c_str());
  int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    return false;
  }
  bool ok = ftruncate(fd, 4096) == 0;
  close(fd);
  return ok;
}

bool
segment_exists(const std::string &name)
{
  int fd = shm_open(name.c_str(), O_RDONLY, 0);
  if (fd < 0) {
    return false;
  }
  close(fd);
  return true;
}

// The kernel rounds an shm object up to a page, so a segment shorter than CONTROL_SIZE is not representable everywhere:
// Apple Silicon's 16 KB page already exceeds it. -1 if the segment is gone.
long long
segment_size(const std::string &name)
{
  int fd = shm_open(name.c_str(), O_RDONLY, 0);
  if (fd < 0) {
    return -1;
  }
  struct stat sb;
  const int   rc = fstat(fd, &sb);
  close(fd);
  return rc < 0 ? -1 : static_cast<long long>(sb.st_size);
}

} // namespace

// A foreign sizeof(CacheShmControl) leaves the frozen header readable but the stripe table not. Purging must fall back to
// the name space, or every stripe segment leaks while `traffic_ctl cache shm clear` reports success.
TEST_CASE("CacheShm purge sweeps by name when the control layout is foreign", "[cache][shm]")
{
  const std::string prefix      = cache_shm::normalize_name_prefix(PURGE_PREFIX_WORD);
  const std::string stripe_name = cache_shm::stripe_segment_name(prefix, 0);

  // Stands in for a build with a larger stripe table.
  if (!plant_control_segment(prefix, cache_shm::CONTROL_SIZE * 2)) {
    WARN("shm unavailable in this environment; skipping");
    return;
  }
  REQUIRE(plant_stripe_segment(stripe_name));

  const cache_shm::PurgeReport report = cache_shm::purge_segments(prefix);

  CHECK(report.outcome == cache_shm::PurgeOutcome::Purged);
  CHECK(report.table_untrusted);
  CHECK_FALSE(segment_exists(stripe_name));
  CHECK_FALSE(segment_exists(cache_shm::control_segment_name(prefix)));

  shm_unlink(stripe_name.c_str());
  shm_unlink(cache_shm::control_segment_name(prefix).c_str());
}

// The same-size case must walk the table rather than sweep, so a shared name space is not over-swept.
TEST_CASE("CacheShm purge walks the table when the control layout is ours", "[cache][shm]")
{
  const std::string prefix = cache_shm::normalize_name_prefix(PURGE_PREFIX_WORD);

  if (!plant_control_segment(prefix, cache_shm::CONTROL_SIZE)) {
    WARN("shm unavailable in this environment; skipping");
    return;
  }

  // Absent from the table, so a sweep removes it and a table walk does not. Without this the assertion is vacuous: the
  // sweep only records successes, so an empty table yields an identical report either way.
  const std::string stray = cache_shm::stripe_segment_name(prefix, 0);
  REQUIRE(plant_stripe_segment(stray));

  const cache_shm::PurgeReport report = cache_shm::purge_segments(prefix);

  CHECK(report.outcome == cache_shm::PurgeOutcome::Purged);
  CHECK_FALSE(report.table_untrusted);
  CHECK(segment_exists(stray));

  shm_unlink(stray.c_str());
  shm_unlink(cache_shm::control_segment_name(prefix).c_str());
}

// A segment shorter than our CacheShmControl was written by an *older* build, which may still be running. The frozen
// header is there so a newer traffic_ctl can recognise that owner, not so it can unlink the names out from under it.
TEST_CASE("CacheShm purge refuses a smaller foreign control segment with a live owner", "[cache][shm]")
{
  const std::string prefix      = cache_shm::normalize_name_prefix(PURGE_PREFIX_WORD);
  const std::string control     = cache_shm::control_segment_name(prefix);
  const std::string stripe_name = cache_shm::stripe_segment_name(prefix, 0);

  // Past the frozen header so the owner is readable, short of CONTROL_SIZE so the table cannot be walked.
  if (!plant_control_segment(prefix, cache_shm::CONTROL_HEADER_SIZE + 64, static_cast<int32_t>(getpid()))) {
    WARN("shm unavailable in this environment; skipping");
    return;
  }
  if (segment_size(control) >= static_cast<long long>(cache_shm::CONTROL_SIZE)) {
    WARN("shm objects round up past CONTROL_SIZE here; a smaller foreign segment is not representable");
    shm_unlink(control.c_str());
    return;
  }
  REQUIRE(plant_stripe_segment(stripe_name));

  // Hold the lock too, so the refusal is asserted on both kinds of platform: flock decides where it is honoured (a second
  // open file description conflicts even within one process), owner_pid where it is not.
  int held = shm_open(control.c_str(), O_RDONLY, 0);
  REQUIRE(held >= 0);
  (void)::flock(held, LOCK_EX | LOCK_NB);

  const cache_shm::PurgeReport report = cache_shm::purge_segments(prefix);

  CHECK(report.outcome == cache_shm::PurgeOutcome::OwnedByLive);
  CHECK(report.unlinked.empty());
  CHECK(segment_exists(stripe_name));
  CHECK(segment_exists(control));

  close(held);
  shm_unlink(stripe_name.c_str());
  shm_unlink(control.c_str());
}

// The counterpart: with no live owner the same short segment must still be cleared, or an operator cannot recover from a
// stale one left by a build that is gone.
TEST_CASE("CacheShm purge clears a smaller foreign control segment with no owner", "[cache][shm]")
{
  const std::string prefix      = cache_shm::normalize_name_prefix(PURGE_PREFIX_WORD);
  const std::string control     = cache_shm::control_segment_name(prefix);
  const std::string stripe_name = cache_shm::stripe_segment_name(prefix, 0);

  if (!plant_control_segment(prefix, cache_shm::CONTROL_HEADER_SIZE + 64)) {
    WARN("shm unavailable in this environment; skipping");
    return;
  }
  if (segment_size(control) >= static_cast<long long>(cache_shm::CONTROL_SIZE)) {
    WARN("shm objects round up past CONTROL_SIZE here; a smaller foreign segment is not representable");
    shm_unlink(control.c_str());
    return;
  }
  REQUIRE(plant_stripe_segment(stripe_name));

  const cache_shm::PurgeReport report = cache_shm::purge_segments(prefix);

  CHECK(report.outcome == cache_shm::PurgeOutcome::TooSmall);
  CHECK(report.table_untrusted);
  CHECK_FALSE(segment_exists(stripe_name));
  CHECK_FALSE(segment_exists(control));

  shm_unlink(stripe_name.c_str());
  shm_unlink(control.c_str());
}

#endif // TS_USE_CACHE_SHM
