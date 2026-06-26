/** @file

  Shared-memory-backed cache directory for fast restart.

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

#include "CacheShm.h"
#include "CacheShmLayout.h"
#include "CacheShmPurge.h"

#include "P_CacheDir.h"
#include "iocore/cache/Store.h"

#include "records/RecCore.h"
#include "tscore/Diags.h"
#include "tscore/HashFNV.h"
#include "tscore/ink_align.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_string.h"
#include "tsutil/DbgCtl.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{

DbgCtl dbg_ctl{"cache_shm"};

using cache_shm::CACHE_SHM_MAGIC;
using cache_shm::CACHE_SHM_SCHEMA_VERSION;
using cache_shm::CacheShmControl;
using cache_shm::control_segment_name;
using cache_shm::CONTROL_SIZE;
using cache_shm::LockResult;
using cache_shm::MAX_SHM_NAME_LEN;
using cache_shm::MAX_STRIPES;
using cache_shm::read_shm_name;
using cache_shm::StripeEntry;
using cache_shm::try_lock_control;

// Sanity bound: the control struct (header + stripe table) must stay small.
constexpr std::size_t MAX_CONTROL_SEGMENT_BYTES = 32 * 1024;
static_assert(sizeof(CacheShmControl) <= MAX_CONTROL_SEGMENT_BYTES, "control segment unexpectedly large");

// Configuration loaded at initialize() time.
struct Config {
  bool        enabled              = false;
  bool        use_hugepages        = false;
  bool        purge_stale_on_start = false;
  std::string name_prefix          = "/ats-"; // normalized "/<word>-" (see normalize_name_prefix); set in load_config.
};

Config g_config;

// Live state for the open control segment.
CacheShmControl *g_control = nullptr;
std::string      g_control_name;

// Holds the control segment's exclusive flock for the process lifetime; the OS
// releases it on exit. Only set on the path that owns the segment.
ats_scoped_fd g_control_fd;

// shm pointers we returned (mapped to their length), so the Stripe destructor can
// choose munmap vs ats_free and detach_stripe can munmap the right span.
std::mutex                              g_pointers_mutex;
std::unordered_map<char *, std::size_t> g_pointers;

// Guards the control-segment stripe table and the per-run claim bookkeeping below
// (stripes initialize concurrently across disk threads).
std::mutex g_table_mutex;

// Per-run partial-attach bookkeeping, indexed in lockstep with g_control->stripes[].
// An entry still unclaimed once init completes is an orphan reclaimed by
// finalize_attach(). Process-local, reset each run.
bool     g_entry_claimed[MAX_STRIPES] = {};
uint32_t g_claims_this_run            = 0;

void
fnv_update(ATSHash64FNV1a &h, uint64_t v)
{
  h.update(&v, sizeof v);
}

/// Full 64-bit stripe identity used to match a stripe to its prior shm segment.
uint64_t
compute_stripe_key_hash(const char *stripe_key)
{
  ATSHash64FNV1a hash;
  hash.update(stripe_key, std::strlen(stripe_key));
  return hash.get();
}

/// Build a stripe shm name from its per-host index (unique, so names never
/// collide). Matching to a prior segment uses the key hash, not this name.
std::string
build_stripe_shm_name(const std::string &prefix, uint32_t stripe_index)
{
  std::string name = prefix + "s" + std::to_string(stripe_index);
  if (name.size() >= MAX_SHM_NAME_LEN) {
    name.resize(MAX_SHM_NAME_LEN - 1);
  }
  return name;
}

// Named flags for open_and_map_shm so call sites read `ShmAccess::Create` /
// `HugePages::Off` and the two can't be transposed.
enum class ShmAccess { Open, Create };
enum class HugePages { Off, On };

/// Open or create a shm segment of `size` bytes and mmap it. Returns nullptr
/// on failure. When `out_fd` is non-null, the open fd is handed back to the
/// caller (left open) so it can hold an flock on the segment; otherwise the fd
/// is closed once the mapping is established (the mmap survives the close).
/// When `out_errno` is non-null it receives the failing syscall's errno (0 on
/// success) so the caller can render a non-opaque diagnostic.
void *
open_and_map_shm(const std::string &name, std::size_t size, ShmAccess access, [[maybe_unused]] HugePages hugepages,
                 int *out_fd = nullptr, int *out_errno = nullptr)
{
  if (out_errno != nullptr) {
    *out_errno = 0;
  }
  int oflags = O_RDWR;
  if (access == ShmAccess::Create) {
    oflags |= O_CREAT;
  }

  ats_scoped_fd fd{shm_open(name.c_str(), oflags, 0600)};
  if (fd < 0) {
    int e = errno;
    Dbg(dbg_ctl, "shm_open(%s, %s) failed: %s", name.c_str(), access == ShmAccess::Create ? "create" : "open", strerror(e));
    if (out_errno != nullptr) {
      *out_errno = e;
    }
    return nullptr;
  }

  if (access == ShmAccess::Create) {
    if (ftruncate(fd, size) < 0) {
      int e = errno;
      Warning("ftruncate(%s, %zu) failed: %s", name.c_str(), size, strerror(e));
      shm_unlink(name.c_str());
      if (out_errno != nullptr) {
        *out_errno = e;
      }
      return nullptr;
    }
  } else {
    // The kernel rounds shm size up to a page boundary (16 KiB on macOS / Apple
    // Silicon), so accept any size in [requested, page-up].
    struct stat sb {
    };
    std::size_t expected_max = INK_ALIGN(size, ats_pagesize());
    if (fstat(fd, &sb) < 0 || sb.st_size < 0 || static_cast<std::size_t>(sb.st_size) < size ||
        static_cast<std::size_t>(sb.st_size) > expected_max) {
      Dbg(dbg_ctl, "shm %s size mismatch (have %lld, want %zu, max %zu)", name.c_str(), static_cast<long long>(sb.st_size), size,
          expected_max);
      return nullptr;
    }
  }

  int   prot  = PROT_READ | PROT_WRITE;
  int   flags = MAP_SHARED;
  void *addr  = mmap(nullptr, size, prot, flags, fd, 0);
  if (addr == MAP_FAILED) {
    int e = errno;
    Warning("mmap(%s, %zu) failed: %s", name.c_str(), size, strerror(e));
    if (out_errno != nullptr) {
      *out_errno = e;
    }
    return nullptr;
  }

  // Advise shmem THP for the mapping (cuts page-table teardown at exit). MAP_HUGETLB
  // is not usable here: shm_open fds are tmpfs-backed, so it always EINVALs. Requires
  // shmem THP enabled on the host; see the design doc for details.
#if defined(MADV_HUGEPAGE)
  if (hugepages == HugePages::On) {
    if (madvise(addr, size, MADV_HUGEPAGE) != 0) {
      Dbg(dbg_ctl, "madvise(MADV_HUGEPAGE) on %s failed: %s", name.c_str(), strerror(errno));
    }
  }
#endif

  if (out_fd != nullptr) {
    *out_fd = fd.release(); // caller owns the fd and keeps it open for flock
  }
  return addr;
}

void
unlink_all_known_segments()
{
  if (g_control != nullptr) {
    for (uint32_t i = 0; i < g_control->stripe_count && i < MAX_STRIPES; ++i) {
      std::string name = read_shm_name(g_control->stripes[i].shm_name);
      if (!name.empty()) {
        Dbg(dbg_ctl, "shm_unlink stripe %s", name.c_str());
        shm_unlink(name.c_str());
      }
    }
    munmap(g_control, CONTROL_SIZE);
    g_control = nullptr;
  }
  if (!g_control_name.empty()) {
    Dbg(dbg_ctl, "shm_unlink control %s", g_control_name.c_str());
    shm_unlink(g_control_name.c_str());
  }
}

// Purge leftover shm segments when shm is disabled this run (opt-in via
// purge_stale_on_start). Best-effort: logs but never blocks startup. The
// enumerate-and-unlink work is shared with `traffic_ctl cache shm clear`
// (cache_shm::purge_segments); this just renders the result into diags.
void
purge_stale_segments(const std::string &prefix)
{
  const cache_shm::PurgeReport report = cache_shm::purge_segments(prefix);

  switch (report.outcome) {
  case cache_shm::PurgeOutcome::BadPrefix:
    // load_config() already warned about a bad prefix; stay quiet here.
  case cache_shm::PurgeOutcome::NotPresent:
    return; // ENOENT: shm never used with this prefix.
  case cache_shm::PurgeOutcome::OpenFailed:
    Warning("cache shm: cannot open control segment %s to purge stale segments: %s", report.control_name.c_str(),
            strerror(report.sys_errno));
    return;
  case cache_shm::PurgeOutcome::MapFailed:
    Warning("cache shm: mmap of control segment %s failed while purging: %s", report.control_name.c_str(),
            strerror(report.sys_errno));
    return;
  case cache_shm::PurgeOutcome::TooSmall:
    Warning("cache shm: leftover control segment %s is too small to read (%lld bytes); unlinking it", report.control_name.c_str(),
            report.segment_size);
    break; // purge_segments() already unlinked the control object; render the result below.
  case cache_shm::PurgeOutcome::OwnedByLive:
    Warning("cache shm: control segment %s is owned by a live process; leaving stale segments in place",
            report.control_name.c_str());
    return;
  case cache_shm::PurgeOutcome::Purged:
    break;
  }

  for (const auto &u : report.unlinked) {
    if (u.error == 0) {
      Dbg(dbg_ctl, "purge: unlinked %s %s", u.is_control ? "control" : "stripe", u.name.c_str());
    } else if (u.error != ENOENT) {
      Warning("cache shm: failed to unlink %s %s while purging: %s", u.is_control ? "control segment" : "stripe", u.name.c_str(),
              strerror(u.error));
    }
  }

  Note("cache shm: purged stale segments while disabled (removed %u, %u failure(s), prefix '%s')", report.removed(),
       report.failures(), prefix.c_str());
}

bool
load_config()
{
  RecInt enabled   = RecGetRecordInt("proxy.config.cache.shm.enabled").value_or(0);
  g_config.enabled = enabled != 0;

  RecInt use_hugepages   = RecGetRecordInt("proxy.config.cache.shm.use_hugepages").value_or(0);
  g_config.use_hugepages = use_hugepages != 0;

  RecInt purge_stale_on_start   = RecGetRecordInt("proxy.config.cache.shm.purge_stale_on_start").value_or(0);
  g_config.purge_stale_on_start = purge_stale_on_start != 0;

  char        prefix_buf[256] = {0};
  std::string configured      = "ats"; // operator sets only the middle word; framing is added below.
  if (RecGetRecordString("proxy.config.cache.shm.name_prefix", prefix_buf, sizeof(prefix_buf)).has_value() &&
      prefix_buf[0] != '\0') {
    configured = prefix_buf;
  }
  // Frame the configured middle word as "/<word>-" so the leading '/' that POSIX
  // shm_open requires and the '-' separator can never be mis-typed (a carried-over
  // "/ats-" normalizes back to "/ats-" rather than an invalid "//ats--").
  g_config.name_prefix = cache_shm::normalize_name_prefix(configured);

  return g_config.enabled;
}

// Reserve a control-table slot for a stripe about to be created (reusing a
// tombstone if any, else appending). Marks the slot non-empty so a concurrent
// create cannot pick the same index; g_entry_claimed stays clear until the segment
// is mapped. Returns the slot index (and shm name via out_name), or MAX_STRIPES
// when the table is full. Caller must hold g_table_mutex.
uint32_t
reserve_stripe_slot(uint64_t key_hash, std::size_t directory_size, std::string &out_name)
{
  uint32_t idx        = g_control->stripe_count;
  bool     reuse_slot = false;
  for (uint32_t i = 0; i < g_control->stripe_count && i < MAX_STRIPES; ++i) {
    if (g_control->stripes[i].shm_name[0] == '\0') {
      idx        = i;
      reuse_slot = true;
      break;
    }
  }
  if (!reuse_slot && g_control->stripe_count >= MAX_STRIPES) {
    Warning("cache shm: stripe count exceeds MAX_STRIPES (%zu); falling back", MAX_STRIPES);
    return MAX_STRIPES;
  }

  out_name = build_stripe_shm_name(g_config.name_prefix, idx);
  if (!reuse_slot) {
    g_control->stripe_count++;
  }
  StripeEntry &e = g_control->stripes[idx];
  ink_strlcpy(e.shm_name, out_name.c_str(), sizeof(e.shm_name));
  e.raw_dir_size    = directory_size;
  e.stripe_key_hash = key_hash;
  return idx;
}

// Undo a reserve_stripe_slot() reservation when the segment could not be created.
// Tombstones the slot (empty shm_name) for reuse. Caller must hold g_table_mutex.
void
release_reserved_slot(uint32_t idx)
{
  StripeEntry &e    = g_control->stripes[idx];
  e.shm_name[0]     = '\0';
  e.raw_dir_size    = 0;
  e.stripe_key_hash = 0;
}

// Record a freshly mapped stripe segment as claimed for this run and remember its
// pointer and length (for is_shm_pointer / invalidate_stripe_directory /
// detach_stripe). Takes the locks itself so the shm syscalls that produced `p` ran
// without g_table_mutex held.
char *
claim_mapped_stripe(uint32_t idx, void *p, std::size_t size)
{
  {
    std::scoped_lock lk{g_table_mutex};
    g_entry_claimed[idx] = true;
    ++g_claims_this_run;
  }
  {
    std::scoped_lock plk{g_pointers_mutex};
    g_pointers.insert({static_cast<char *>(p), size});
  }
  return static_cast<char *>(p);
}

} // namespace

CacheShm::Mode CacheShm::_mode = CacheShm::Mode::Disabled;

uint64_t
CacheShm::abi_hash()
{
  ATSHash64FNV1a h;
  h.update(tag.data(), tag.size());
  fnv_update(h, sizeof(Dir));
  fnv_update(h, sizeof(StripeHeaderFooter));
  fnv_update(h, sizeof(CacheShmControl));
  fnv_update(h, sizeof(StripeEntry));
  fnv_update(h, DIR_DEPTH);
  fnv_update(h, SIZEOF_DIR);
  fnv_update(h, MAX_STRIPES);
  return h.get();
}

uint64_t
CacheShm::storage_signature(const Store &store)
{
  ATSHash64FNV1a h;
  for (unsigned i = 0; i < store.n_spans; ++i) {
    const Span *span = store.spans[i];
    if (span == nullptr) {
      continue;
    }
    if (span->pathname) {
      std::string_view path{span->pathname.get()};
      h.update(path.data(), path.size());
    }
    fnv_update(h, static_cast<uint64_t>(span->blocks));
    fnv_update(h, static_cast<uint64_t>(span->offset));
    fnv_update(h, static_cast<uint64_t>(span->hw_sector_size));
  }
  return h.get();
}

void
CacheShm::initialize(const Store &store)
{
  if (!load_config()) {
    _mode = Mode::Disabled;
    // shm is off this run; reclaim any leftover segments from a prior run (rationale
    // and guards documented on purge_stale_segments). Opt-in and best-effort.
    if (g_config.purge_stale_on_start) {
      purge_stale_segments(g_config.name_prefix);
    }
    Dbg(dbg_ctl, "shm disabled");
    return;
  }

  g_control_name = control_segment_name(g_config.name_prefix);
  if (g_control_name.size() >= MAX_SHM_NAME_LEN) {
    Warning("shm name_prefix too long (control segment name '%s' exceeds %zu chars); shm disabled", g_control_name.c_str(),
            MAX_SHM_NAME_LEN);
    _mode = Mode::Disabled;
    return;
  }

  const uint64_t expected_abi       = abi_hash();
  const uint64_t expected_signature = storage_signature(store);

  // Try to attach an existing control segment first.
  int   existing_fd = -1;
  void *existing    = open_and_map_shm(g_control_name, CONTROL_SIZE, ShmAccess::Open, HugePages::Off, &existing_fd);
  if (existing != nullptr) {
    auto *ctrl = static_cast<CacheShmControl *>(existing);

    // Concurrent-attach guard: refuse shm (and rebuild from disk) if another live
    // process still owns this segment.
    int              flock_errno = 0;
    const LockResult lock        = try_lock_control(existing_fd, &flock_errno);
    bool             live_owner  = false;
    switch (lock) {
    case LockResult::Acquired:
      break; // we hold the exclusive lock, so any prior owner is gone
    case LockResult::HeldByOther:
      live_owner = true;
      break;
    case LockResult::Unsupported: // macOS POSIX shm: flock is a no-op, fall back to owner_pid
      Dbg(dbg_ctl, "flock unsupported for control segment %s (errno %d: %s); using owner-pid liveness guard",
          g_control_name.c_str(), flock_errno, strerror(flock_errno));
      live_owner = ctrl->owner_pid != 0 && ctrl->owner_pid != static_cast<int32_t>(getpid()) && process_is_alive(ctrl->owner_pid);
      break;
    }
    if (live_owner) {
      Warning("cache shm: control segment %s has a live owner (pid %d); disabling shm this run to avoid concurrent attach",
              g_control_name.c_str(), ctrl->owner_pid);
      munmap(existing, CONTROL_SIZE);
      close(existing_fd);
      _mode = Mode::Disabled;
      return;
    }

    bool ok = std::memcmp(ctrl->magic, CACHE_SHM_MAGIC, sizeof(CACHE_SHM_MAGIC)) == 0;
    if (ok && ctrl->schema_version != CACHE_SHM_SCHEMA_VERSION) {
      Note("cache shm: schema mismatch (%u vs %u), dropping", ctrl->schema_version, CACHE_SHM_SCHEMA_VERSION);
      ok = false;
    }
    if (ok && ctrl->abi_hash != expected_abi) {
      Note("cache shm: ABI mismatch, dropping");
      ok = false;
    }

    // storage_signature is NOT a hard gate (see storage_signature() doc): a
    // storage.config change keeps the segment, each stripe attaches by its own
    // identity. Refreshed in place below.
    const bool storage_changed = ok && ctrl->storage_signature != expected_signature;

    if (ok && ctrl->clean_shutdown == 0) {
      // A crash may have left dir entries pointing at content never flushed, so no
      // stripe can safely skip recovery -- whole-segment drop.
      Note("cache shm: previous run did not shutdown cleanly, dropping");
      ok = false;
    }

    if (ok) {
      Note("cache shm: attaching up to %u stripes (fast restart%s)", ctrl->stripe_count,
           storage_changed ? ", partial -- storage changed" : "");
      g_control    = ctrl;
      g_control_fd = existing_fd; // hold the exclusive lock for the process lifetime
      std::memset(g_entry_claimed, 0, sizeof(g_entry_claimed));
      g_claims_this_run = 0;
      if (storage_changed) {
        g_control->storage_signature = expected_signature;
      }
      // Become owner and clear clean_shutdown so a crash this run drops shm next time.
      g_control->owner_pid      = static_cast<int32_t>(getpid());
      g_control->clean_shutdown = 0;
      msync(g_control, CONTROL_SIZE, MS_SYNC);
      _mode = Mode::AttachExisting;
      return;
    }

    // Drop everything and fall through to fresh-create. We hold the exclusive lock,
    // so unlinking cannot pull segments out from under a live owner.
    g_control = ctrl; // so unlink_all_known_segments can iterate stripes
    unlink_all_known_segments();
    close(existing_fd); // releases the lock on the now-unlinked object
  }

  // Create fresh control segment.
  int   fresh_fd     = -1;
  int   create_errno = 0;
  void *fresh        = open_and_map_shm(g_control_name, CONTROL_SIZE, ShmAccess::Create, HugePages::Off, &fresh_fd, &create_errno);
  if (fresh == nullptr) {
    // Surface the errno + offending name: e.g. an embedded '/' in name_prefix yields EINVAL here.
    Warning("cache shm: failed to create control segment %s: %s; shm disabled", g_control_name.c_str(), strerror(create_errno));
    _mode = Mode::Disabled;
    return;
  }
  // Lock the freshly created segment. Another starting process could have created
  // and locked it first in the window since the drop above; if so, refuse.
  if (try_lock_control(fresh_fd) == LockResult::HeldByOther) {
    Warning("cache shm: lost the create race for control segment %s; disabling shm this run", g_control_name.c_str());
    munmap(fresh, CONTROL_SIZE);
    close(fresh_fd);
    _mode = Mode::Disabled;
    return;
  }
  g_control    = static_cast<CacheShmControl *>(fresh);
  g_control_fd = fresh_fd; // hold the exclusive lock for the process lifetime
  std::memset(g_control, 0, CONTROL_SIZE);
  std::memset(g_entry_claimed, 0, sizeof(g_entry_claimed));
  g_claims_this_run = 0;
  std::memcpy(g_control->magic, CACHE_SHM_MAGIC, sizeof(CACHE_SHM_MAGIC));
  g_control->schema_version    = CACHE_SHM_SCHEMA_VERSION;
  g_control->abi_hash          = expected_abi;
  g_control->storage_signature = expected_signature;
  g_control->clean_shutdown    = 0;
  g_control->owner_pid         = static_cast<int32_t>(getpid());
  g_control->stripe_count      = 0;

  _mode = Mode::CreateFresh;
  Note("cache shm: creating fresh control segment %s (owner pid %d)", g_control_name.c_str(), static_cast<int>(getpid()));
  return;
}

char *
CacheShm::attach_or_create_stripe(const char *stripe_key, std::size_t directory_size)
{
  if (_mode == Mode::Disabled || g_control == nullptr) {
    return nullptr;
  }

  const uint64_t  key_hash  = compute_stripe_key_hash(stripe_key);
  const HugePages hugepages = g_config.use_hugepages ? HugePages::On : HugePages::Off;

  // Decide what to do under the table lock, but run the shm syscalls afterwards
  // with the lock dropped (holding it across them would serialize every disk
  // thread's init). Each stripe owns a distinct segment, so the syscalls never
  // touch another thread's segment.
  std::string attach_name; // non-empty => map this existing segment
  std::string create_name; // set when a fresh slot was reserved (the create path)
  uint32_t    idx = MAX_STRIPES;
  {
    std::scoped_lock lk{g_table_mutex};

    // 1. Try to attach this stripe's prior segment, matched by 64-bit identity (not
    //    name), so a span going offline shifts indices but not identities.
    for (uint32_t i = 0; i < g_control->stripe_count && i < MAX_STRIPES; ++i) {
      StripeEntry &e = g_control->stripes[i];
      if (e.shm_name[0] == '\0' || e.stripe_key_hash != key_hash) {
        continue; // tombstoned slot, or a different stripe
      }
      if (e.raw_dir_size != directory_size) {
        // Same identity, different size: shouldn't happen (size derives from the
        // keyed blocks). Treat as a miss and recreate; the stale entry is reaped by
        // finalize_attach().
        Note("cache shm: stripe %s size mismatch (have %llu, want %zu); recreating", read_shm_name(e.shm_name).c_str(),
             static_cast<unsigned long long>(e.raw_dir_size), directory_size);
        break;
      }
      attach_name = read_shm_name(e.shm_name);
      idx         = i;
      break;
    }

    // 2. No usable prior segment -- reserve a slot for a fresh create under the lock.
    if (attach_name.empty() && (idx = reserve_stripe_slot(key_hash, directory_size, create_name)) == MAX_STRIPES) {
      return nullptr; // table full (already logged)
    }
  }

  // Attach path: map the existing segment outside the lock.
  if (!attach_name.empty()) {
    void *p = open_and_map_shm(attach_name, directory_size, ShmAccess::Open, hugepages);
    if (p != nullptr) {
      Note("cache shm: attached stripe %s (%zu bytes) for key=%s", attach_name.c_str(), directory_size, stripe_key);
      return claim_mapped_stripe(idx, p, directory_size);
    }
    // Attach failed (segment vanished/unmappable): reserve a fresh slot and fall
    // through to create. The stale entry is reaped by finalize_attach().
    Note("cache shm: failed to attach stripe %s; recreating", attach_name.c_str());
    std::scoped_lock lk{g_table_mutex};
    if ((idx = reserve_stripe_slot(key_hash, directory_size, create_name)) == MAX_STRIPES) {
      return nullptr;
    }
  }

  // Create path: slot already reserved; syscalls run outside the lock. A fresh
  // ftruncate'd segment is zero-filled (magic 0), so Stripe::init falls back to the
  // disk read and repopulates it. shm_unlink clears any leftover with this name.
  shm_unlink(create_name.c_str());
  void *p = open_and_map_shm(create_name, directory_size, ShmAccess::Create, hugepages);
  if (p == nullptr) {
    std::scoped_lock lk{g_table_mutex};
    release_reserved_slot(idx);
    return nullptr;
  }

  Note("cache shm: created stripe %s (%zu bytes) for key=%s", create_name.c_str(), directory_size, stripe_key);
  return claim_mapped_stripe(idx, p, directory_size);
}

void
CacheShm::finalize_attach()
{
  if (g_control == nullptr) {
    return;
  }

  std::scoped_lock lk{g_table_mutex};

  // With zero claims this run we cannot distinguish "genuinely empty cache" from
  // "init aborted" (e.g. a transient volume.config error), so leave every segment
  // intact rather than risk reclaiming a valid cache.
  if (g_claims_this_run == 0) {
    Dbg(dbg_ctl, "finalize_attach: no stripes claimed this run; leaving %u segment(s) intact", g_control->stripe_count);
    return;
  }

  uint32_t reclaimed = 0;
  for (uint32_t i = 0; i < g_control->stripe_count && i < MAX_STRIPES; ++i) {
    StripeEntry &e = g_control->stripes[i];
    if (e.shm_name[0] == '\0' || g_entry_claimed[i]) {
      continue; // already empty, or claimed by a live stripe this run
    }
    // Unclaimed, non-empty entry: its stripe left the cache (span dropped, or disk
    // failed to open). Unlink the orphan and tombstone the slot for reuse.
    std::string name = read_shm_name(e.shm_name);
    Note("cache shm: reclaiming orphaned stripe segment %s", name.c_str());
    shm_unlink(name.c_str());
    e.shm_name[0]     = '\0';
    e.raw_dir_size    = 0;
    e.stripe_key_hash = 0;
    ++reclaimed;
  }
  if (reclaimed > 0) {
    Note("cache shm: reclaimed %u orphaned stripe segment(s) after storage change", reclaimed);
  }

  // Trim trailing tombstones so stripe_count tracks the live high-water mark;
  // interior tombstones stay (reused by attach_or_create_stripe).
  uint32_t live_count = 0;
  for (uint32_t i = 0; i < g_control->stripe_count && i < MAX_STRIPES; ++i) {
    if (g_control->stripes[i].shm_name[0] != '\0') {
      live_count = i + 1;
    }
  }
  const bool count_changed = live_count != g_control->stripe_count;
  if (count_changed) {
    Note("cache shm: trimming stripe_count %u -> %u after reclaim", g_control->stripe_count, live_count);
    g_control->stripe_count = live_count;
  }

  if (reclaimed > 0 || count_changed) {
    msync(g_control, CONTROL_SIZE, MS_SYNC);
  }
}

bool
CacheShm::is_shm_pointer(char *raw_dir)
{
  if (raw_dir == nullptr) {
    return false;
  }
  std::scoped_lock lk{g_pointers_mutex};
  return g_pointers.find(raw_dir) != g_pointers.end();
}

void
CacheShm::mark_clean_shutdown()
{
  if (g_control == nullptr) {
    return;
  }
  Note("cache shm: marking clean shutdown");
  g_control->clean_shutdown = 1;
  // Clear owner_pid so the next start's liveness backstop does not mistake our
  // (exiting) PID for a live owner. The flock is still held until exit, so a
  // concurrent starter is still correctly refused during the shutdown window.
  g_control->owner_pid = 0;
  msync(g_control, CONTROL_SIZE, MS_SYNC);
}

bool
CacheShm::process_is_alive(int pid)
{
  return cache_shm::process_is_alive(pid);
}

void
CacheShm::invalidate_stripe_directory(char *raw_dir)
{
  if (!is_shm_pointer(raw_dir)) {
    return;
  }
  // Zero the in-shm header magic so Stripe::init's attach gate rejects this segment
  // next start and recovers the stripe from disk instead of fast-attaching a
  // directory we could not finish flushing.
  auto *header  = reinterpret_cast<StripeHeaderFooter *>(raw_dir);
  header->magic = 0;
  msync(raw_dir, sizeof(StripeHeaderFooter), MS_SYNC);
}

void
CacheShm::detach_stripe(char *raw_dir)
{
  if (raw_dir == nullptr) {
    return;
  }
  std::scoped_lock lk{g_pointers_mutex};
  auto             it = g_pointers.find(raw_dir);
  if (it == g_pointers.end()) {
    return;
  }
  // munmap the recorded span; never shm_unlink -- the segment must survive for the
  // next start to attach.
  munmap(it->first, it->second);
  g_pointers.erase(it);
}
