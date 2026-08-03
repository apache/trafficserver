/** @file

  Unit tests for StripeSM::shutdown with a shm-backed directory. These need shm actually enabled, which is a process-wide
  mode, so they live in their own binary rather than alongside the heap-backed stripe tests.

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
#include "test_doubles.h"

#include "../CacheShm.h"
#include "shared/cache_shm/Layout.h"
#include "shared/cache_shm/Purge.h"
#include "../P_CacheInternal.h"

#include "iocore/cache/Store.h"
#include "records/RecCore.h"

#include <cstdio>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

// Required by main.h
int  cache_vols           = 1;
bool reuse_existing_cache = false;

namespace
{

// Our own prefix so these can never touch a real instance's segments, short enough to stay under the 31-char POSIX limit.
constexpr const char *TEST_PREFIX_WORD = "atsunittest";

// False when shm is unavailable here, e.g. a sandbox forbidding shm_open, so the test skips rather than fails.
bool
enable_shm()
{
  REQUIRE(RecSetRecordInt("proxy.config.cache.shm.enabled", 1, REC_SOURCE_EXPLICIT) == REC_ERR_OKAY);
  REQUIRE(RecSetRecordString("proxy.config.cache.shm.name_prefix", TEST_PREFIX_WORD, REC_SOURCE_EXPLICIT) == REC_ERR_OKAY);

  Store store;
  CacheShm::initialize(store);
  return CacheShm::mode() != CacheShm::Mode::Disabled;
}

// shm_unlink only removes the name and live mappings stay valid, so this is safe while a stripe still holds one.
void
unlink_test_segments()
{
  const std::string prefix = cache_shm::normalize_name_prefix(TEST_PREFIX_WORD);
  shm_unlink(cache_shm::control_segment_name(prefix).c_str());
  for (uint32_t i = 0; i < 4; ++i) {
    shm_unlink(cache_shm::stripe_segment_name(prefix, i).c_str());
  }
}

// The trust mark lives in the control segment, so shutdown can never reach it through raw_dir. Reads it back the way
// traffic_ctl does, out of a second mapping of the same object.
bool
stripe_marked_untrusted(uint32_t idx)
{
  const std::string prefix = cache_shm::normalize_name_prefix(TEST_PREFIX_WORD);
  int               fd     = shm_open(cache_shm::control_segment_name(prefix).c_str(), O_RDONLY, 0600);
  REQUIRE(fd >= 0);
  void *addr = mmap(nullptr, cache_shm::CONTROL_SIZE, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  REQUIRE(addr != MAP_FAILED);
  auto      *ctrl   = static_cast<cache_shm::CacheShmControl *>(addr);
  const bool marked = idx < ctrl->stripe_count && ctrl->stripes[idx].dir_untrusted != 0;
  munmap(addr, cache_shm::CONTROL_SIZE);
  return marked;
}

// Refusing a marked entry must reuse its slot, or stripe_count creeps toward MAX_STRIPES across restarts.
uint32_t
control_stripe_count()
{
  const std::string prefix = cache_shm::normalize_name_prefix(TEST_PREFIX_WORD);
  int               fd     = shm_open(cache_shm::control_segment_name(prefix).c_str(), O_RDONLY, 0600);
  REQUIRE(fd >= 0);
  void *addr = mmap(nullptr, cache_shm::CONTROL_SIZE, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  REQUIRE(addr != MAP_FAILED);
  const uint32_t count = static_cast<cache_shm::CacheShmControl *>(addr)->stripe_count;
  munmap(addr, cache_shm::CONTROL_SIZE);
  return count;
}

// Reaches the protected gate StripeSM::init() consults before fast-attaching.
struct GateStripe : public StripeSM {
  using StripeSM::StripeSM;
  bool
  shm_directory_is_valid()
  {
    return this->_shm_directory_is_valid();
  }
};

// Reaches the protected aggregation buffer so a shutdown flush failure can be staged.
struct AggStripe : public StripeSM {
  using StripeSM::StripeSM;
  void
  stage_pending_bytes(int nbytes)
  {
    this->_write_buffer.seek(nbytes);
  }
};

} // namespace

// The mark that drops a stripe next start must not be written through raw_dir: directory.header aliases it, and both the
// shutdown pwrite and the periodic dir sync copy that buffer to disk, so a mark there clears the stripe instead.
TEST_CASE("StripeSM::shutdown marks the stripe untrusted without touching the directory", "[cache][shm]")
{
  unlink_test_segments();
  if (!enable_shm()) {
    WARN("shm unavailable in this environment; skipping");
    unlink_test_segments();
    return;
  }

  CacheDisk disk;
  init_disk(disk);
  CacheVol cache_vol;
  {
    StripeSM stripe{&disk, 10, 0};
    stripe.cache_vol = &cache_vol;

    auto *file{attach_tmpfile_to_stripe(stripe)};

    // The directory must really live in shm, or the invalidation under test is a no-op.
    REQUIRE(CacheShm::is_shm_pointer(stripe.directory.raw_dir));

    stripe.clear_dir();
    REQUIRE(stripe.directory.header->magic == STRIPE_MAGIC);

    // Together these take the invalidate-then-still-write path.
    stripe.io.aiocb.aio_fildes     = stripe.fd;
    stripe.directory.header->dirty = 1;

    {
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      stripe.shutdown(this_ethread());
    }

    // The mark goes to the control segment; raw_dir -- which is also the pwrite source -- must come through untouched, so
    // the disk copy stays loadable. shutdown() leaves the stripe locked, so read the file unlocked.
    CHECK(stripe_marked_untrusted(0));
    CHECK(stripe.directory.header->magic == STRIPE_MAGIC);

    StripeHeaderFooter on_disk{};
    std::size_t        headers_read{};
    const uint32_t     sync_serial = stripe.directory.footer->sync_serial;
    fseek(file, stripe.skip + ((sync_serial & 1) ? stripe.dirlen() : 0), SEEK_SET);
    headers_read = fread(&on_disk, sizeof(on_disk), 1, file);
    REQUIRE(1 == headers_read);

    CHECK(STRIPE_MAGIC == on_disk.magic);
    CHECK(sync_serial == on_disk.sync_serial);
  }

  unlink_test_segments();
}

// The mark only pays off if the next start honours it. A clean reuse runs first so a fresh segment in the second half
// cannot be mistaken for the control segment having been dropped for some unrelated reason.
TEST_CASE("An untrusted stripe entry is recreated instead of attached", "[cache][shm]")
{
  unlink_test_segments();
  if (!enable_shm()) {
    WARN("shm unavailable in this environment; skipping");
    unlink_test_segments();
    return;
  }

  constexpr std::size_t dir_size = 8192;
  constexpr const char *key      = " 0:99";
  constexpr char        sentinel = 0x5a;

  char *first = CacheShm::attach_or_create_stripe(key, dir_size);
  REQUIRE(first != nullptr);
  first[0] = sentinel;
  CacheShm::mark_clean_shutdown();
  CacheShm::detach_stripe(first);
  const uint32_t count_before = control_stripe_count();

  // Baseline: an unmarked entry is reused, so the sentinel survives the restart.
  CacheShm::release_for_test();
  REQUIRE(enable_shm());
  REQUIRE(CacheShm::mode() == CacheShm::Mode::AttachExisting);
  char *reattached = CacheShm::attach_or_create_stripe(key, dir_size);
  REQUIRE(reattached != nullptr);
  REQUIRE(reattached[0] == sentinel);

  reattached[0] = sentinel;
  CacheShm::invalidate_stripe_directory(reattached);
  CacheShm::mark_clean_shutdown();
  CacheShm::detach_stripe(reattached);

  // Same key, same size, entry still present -- but marked, so a fresh (zero-filled) segment must come back instead.
  CacheShm::release_for_test();
  REQUIRE(enable_shm());
  REQUIRE(CacheShm::mode() == CacheShm::Mode::AttachExisting);
  char *fresh = CacheShm::attach_or_create_stripe(key, dir_size);
  REQUIRE(fresh != nullptr);
  CHECK(fresh[0] == 0);
  // Refusing the entry must reuse its slot, not orphan it for finalize_attach: a run that never reaches finalize would
  // otherwise leak a slot per restart, and both segments would be mapped at once.
  CHECK(control_stripe_count() == count_before);
  CHECK(!stripe_marked_untrusted(0));
  CacheShm::detach_stripe(fresh);

  unlink_test_segments();
}

// The other invalidate caller, and the only one that returns before the directory write: a bad disk must still be marked,
// but nothing may be pushed to a disk we already gave up on.
TEST_CASE("StripeSM::shutdown marks a bad disk's stripe without writing to it", "[cache][shm]")
{
  unlink_test_segments();
  if (!enable_shm()) {
    WARN("shm unavailable in this environment; skipping");
    unlink_test_segments();
    return;
  }

  CacheDisk disk;
  init_disk(disk);
  CacheVol cache_vol;
  {
    StripeSM stripe{&disk, 10, 0};
    stripe.cache_vol = &cache_vol;

    auto *file{attach_tmpfile_to_stripe(stripe)};
    REQUIRE(CacheShm::is_shm_pointer(stripe.directory.raw_dir));

    disk.hw_sector_size = 512;
    stripe.clear_dir();
    stripe.directory.header->dirty = 1;
    const uint32_t serial_before   = stripe.directory.header->sync_serial;

    SET_DISK_BAD((&disk));
    {
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      stripe.shutdown(this_ethread());
    }

    CHECK(stripe_marked_untrusted(0));
    CHECK(stripe.directory.header->magic == STRIPE_MAGIC);
    // Returned before the sync, so neither the serial nor the untouched A/B slot moved.
    CHECK(stripe.directory.header->sync_serial == serial_before);

    StripeHeaderFooter on_disk{};
    std::size_t        headers_read{};
    fseek(file, stripe.skip + (((serial_before + 1) & 1) ? stripe.dirlen() : 0), SEEK_SET);
    headers_read = fread(&on_disk, sizeof(on_disk), 1, file);
    // Short read means the slot the sync would have used was never written at all, which is the point.
    CHECK((headers_read == 0 || on_disk.sync_serial != serial_before + 1));
  }

  unlink_test_segments();
}

// dir_prev carries tag/phase/head/pinned on an in-use entry, so bounds-checking it there compares flag bits against the
// entry count. This stripe has 4 entries per segment against a head bit of 8192, so a healthy directory looked corrupt.
TEST_CASE("A small stripe's directory validates with in-use entries", "[cache][shm]")
{
  unlink_test_segments();
  if (!enable_shm()) {
    WARN("shm unavailable in this environment; skipping");
    unlink_test_segments();
    return;
  }

  CacheDisk disk;
  init_disk(disk);
  CacheVol cache_vol;
  {
    GateStripe stripe{&disk, 10, 0};
    stripe.cache_vol = &cache_vol;
    attach_tmpfile_to_stripe(stripe);
    // clear_dir() seeds header->sector_size from the disk, and the validator rejects 0.
    disk.hw_sector_size = 512;
    stripe.clear_dir();

    REQUIRE(static_cast<int64_t>(stripe.directory.buckets) * DIR_DEPTH < (1 << 13));
    REQUIRE(stripe.shm_directory_is_valid());

    // Flagged the way a real first fragment is.
    Dir *e = stripe.directory.dir;
    dir_set_offset(e, 1);
    dir_set_head(e, 1);
    dir_set_tag(e, 0xfff);
    REQUIRE(dir_prev(e) > static_cast<int64_t>(stripe.directory.buckets) * DIR_DEPTH);

    CHECK(stripe.shm_directory_is_valid());
  }

  unlink_test_segments();
}

// What the flush-failure path actually leans on. A real short write leaves write_pos in range with agg_pos ahead of it, so
// the quiesced-cursor check has to be what rejects the segment -- not the range check, which only a synthetic write_pos
// trips.
TEST_CASE("An unquiesced write cursor fails the attach gate on its own", "[cache][shm]")
{
  unlink_test_segments();
  if (!enable_shm()) {
    WARN("shm unavailable in this environment; skipping");
    unlink_test_segments();
    return;
  }

  CacheDisk disk;
  init_disk(disk);
  CacheVol cache_vol;
  {
    GateStripe stripe{&disk, 10, 0};
    stripe.cache_vol    = &cache_vol;
    disk.hw_sector_size = 512;
    attach_tmpfile_to_stripe(stripe);
    stripe.clear_dir();
    REQUIRE(stripe.shm_directory_is_valid());

    // Every cursor stays inside [start, skip + len], so only agg_pos != write_pos can be the reason.
    stripe.directory.header->agg_pos = stripe.directory.header->write_pos + 512;
    REQUIRE(stripe.directory.header->agg_pos <= stripe.skip + stripe.len);
    REQUIRE(stripe.directory.header->write_pos >= stripe.start);

    CHECK_FALSE(stripe.shm_directory_is_valid());
  }

  unlink_test_segments();
}

// A failed shutdown flush must still write the on-disk directory -- skipping it drops every insert since the last periodic
// sync -- and must leave the in-segment magic alone, since agg_pos != write_pos already fails the next start's attach gate.
TEST_CASE("StripeSM::shutdown syncs the directory when the agg flush fails", "[cache][shm]")
{
  unlink_test_segments();
  if (!enable_shm()) {
    WARN("shm unavailable in this environment; skipping");
    unlink_test_segments();
    return;
  }

  CacheDisk disk;
  init_disk(disk);
  CacheVol cache_vol;
  {
    AggStripe stripe{&disk, 10, 0};
    stripe.cache_vol = &cache_vol;

    auto *file{attach_tmpfile_to_stripe(stripe)};
    REQUIRE(CacheShm::is_shm_pointer(stripe.directory.raw_dir));

    disk.hw_sector_size = 512;
    stripe.clear_dir();
    REQUIRE(stripe.directory.header->magic == STRIPE_MAGIC);

    // Only the flush must fail, so keep the AIO path out of it.
    stripe.set_io_not_in_progress();
    stripe.directory.header->dirty = 1;

    // A negative write_pos fails only the aggregation pwrite (EINVAL); the directory write is addressed from skip, so it
    // still lands. Breaking the shared fd instead would fail both.
    stripe.stage_pending_bytes(512);
    stripe.directory.header->write_pos = -1;

    const off_t    write_pos_before = stripe.directory.header->write_pos;
    const uint32_t serial_before    = stripe.directory.header->sync_serial;

    {
      SCOPED_MUTEX_LOCK(lock, stripe.mutex, this_ethread());
      stripe.shutdown(this_ethread());
    }

    // Marked like the other two paths that cannot vouch for the directory; the cursor it leaves unquiesced is not durable.
    CHECK(stripe.directory.header->write_pos == write_pos_before);
    CHECK(stripe.directory.header->agg_pos != stripe.directory.header->write_pos);
    CHECK(stripe.directory.header->magic == STRIPE_MAGIC);
    CHECK(stripe_marked_untrusted(0));

    // Pin the slot to the bumped serial: the early return this used to take left the serial alone, so reading the slot it
    // implies would just find what clear_dir() wrote and prove nothing.
    const uint32_t expect_serial = serial_before + 1;
    REQUIRE(stripe.directory.header->sync_serial == expect_serial);

    StripeHeaderFooter on_disk{};
    std::size_t        headers_read{};
    fseek(file, stripe.skip + ((expect_serial & 1) ? stripe.dirlen() : 0), SEEK_SET);
    headers_read = fread(&on_disk, sizeof(on_disk), 1, file);
    REQUIRE(1 == headers_read);

    CHECK(STRIPE_MAGIC == on_disk.magic);
    CHECK(expect_serial == on_disk.sync_serial);
  }

  unlink_test_segments();
}
