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
#include "shared/cache_shm/Layout.h"
#include "shared/cache_shm/Purge.h"

#include "P_CacheDir.h"
#include "iocore/cache/Store.h"

#include "records/RecCore.h"
#include "tscore/Diags.h"
#include "tscore/HashFNV.h"
#include "tscore/hugepages.h"
#include "tscore/ink_align.h"
#include "tscore/ink_config.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_string.h"
#include "tsutil/DbgCtl.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Always compiled: none of these touches an shm syscall, so a build without POSIX shm still gets the real fingerprints
// and their test coverage rather than a stub reporting a zero ABI hash.

CacheShm::Mode CacheShm::_mode = CacheShm::Mode::Disabled;

namespace
{
void
fnv_update(ATSHash64FNV1a &h, uint64_t v)
{
  h.update(&v, sizeof v);
}
} // namespace

uint64_t
CacheShm::abi_hash()
{
  ATSHash64FNV1a h;
  h.update(tag.data(), tag.size());
  fnv_update(h, sizeof(Dir));
  fnv_update(h, sizeof(StripeHeaderFooter));
  fnv_update(h, sizeof(cache_shm::CacheShmControl));
  fnv_update(h, sizeof(cache_shm::StripeEntry));
  fnv_update(h, DIR_DEPTH);
  fnv_update(h, SIZEOF_DIR);
  fnv_update(h, cache_shm::MAX_STRIPES);
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

bool
CacheShm::process_is_alive(int pid)
{
  return cache_shm::process_is_alive(pid);
}

#if !TS_USE_CACHE_SHM

// No POSIX shm in libc; glibc < 2.34 puts shm_open/shm_unlink in librt, which ATS does not link. Mode stays Disabled, so
// every stripe takes the heap path and the cache behaves as it did before this feature existed.

void
CacheShm::initialize(const Store &)
{
  // Only when the operator asked for it; the default is off, so an unconditional note would be noise on every start.
  if (RecGetRecordInt("proxy.config.cache.shm.enabled").value_or(0) != 0) {
    Warning("cache shm: proxy.config.cache.shm.enabled is set, but this build has no POSIX shared memory support "
            "(shm_open is not in libc); using heap directories");
  }
}

char *
CacheShm::attach_or_create_stripe(const char *, std::size_t)
{
  return nullptr;
}

void
CacheShm::finalize_attach()
{
}

bool
CacheShm::is_shm_pointer(char *)
{
  return false;
}

void
CacheShm::mark_clean_shutdown()
{
}

void
CacheShm::invalidate_stripe_directory(char *)
{
}

void
CacheShm::detach_stripe(char *)
{
}

void
CacheShm::release_for_test()
{
}

#else

namespace
{

DbgCtl dbg_ctl{"cache_shm"};

using cache_shm::CACHE_SHM_MAGIC;
using cache_shm::CACHE_SHM_SCHEMA_VERSION;
using cache_shm::CacheShmControl;
using cache_shm::CONTROL_HEADER_SIZE;
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
  std::string name_prefix; // normalized "/<word>-" (see normalize_name_prefix); set in load_config.
};

Config g_config;

// Live state for the open control segment.
CacheShmControl *g_control = nullptr;
std::string      g_control_name;

// Held for the process lifetime so the OS releases it on exit. Only set on the path that owns the segment.
ats_scoped_fd g_control_fd;

// Pointers we returned, so ~Stripe can choose munmap over ats_free and unmap the right span, and so an invalidation can
// reach the stripe's control-table entry without every caller having to carry the index.
struct MappedStripe {
  std::size_t size;
  uint32_t    index;
};
std::mutex                               g_pointers_mutex;
std::unordered_map<char *, MappedStripe> g_pointers;

// Guards the stripe table and the claim bookkeeping below; stripes initialize concurrently across disk threads.
std::mutex g_table_mutex;

// Per-run partial-attach bookkeeping, indexed in lockstep with g_control->stripes[].
// An entry still unclaimed once init completes is an orphan for finalize_attach(). Process-local, reset each run.
bool     g_entry_claimed[MAX_STRIPES] = {};
uint32_t g_claims_this_run            = 0;

/// Full 64-bit stripe identity used to match a stripe to its prior shm segment.
uint64_t
compute_stripe_key_hash(const char *stripe_key)
{
  ATSHash64FNV1a hash;
  hash.update(stripe_key, std::strlen(stripe_key));
  return hash.get();
}

/// Shared with the purge path, which sweeps the index space by name when the stripe table cannot be trusted.
using cache_shm::stripe_segment_name;

// Named so the two cannot be transposed at a call site.
enum class ShmAccess { Open, Create };
enum class HugePages { Off, On };

/// nullptr on failure. `out_fd`, when set, keeps the fd open so the caller can flock it; otherwise it is closed, which
/// the mapping survives.
void *
open_and_map_shm(const std::string &name, std::size_t size, ShmAccess access, [[maybe_unused]] HugePages hugepages,
                 int *out_fd = nullptr, int *out_errno = nullptr)
{
  if (out_errno != nullptr) {
    *out_errno = 0;
  }
  int oflags = O_RDWR;
  if (access == ShmAccess::Create) {
    // O_EXCL so a create never adopts a pre-existing (attacker-planted) object.
    oflags |= O_CREAT | O_EXCL;
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
    // The kernel rounds an shm object up to a page, so accept any size in [requested, page-up].
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
    // Or the leak wedges the next O_EXCL create on EEXIST.
    if (access == ShmAccess::Create) {
      shm_unlink(name.c_str());
    }
    if (out_errno != nullptr) {
      *out_errno = e;
    }
    return nullptr;
  }

  // MAP_HUGETLB is not usable on a tmpfs-backed fd, so advise THP instead; needs shmem THP enabled on the host.
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

/// How the pre-existing control segment could be mapped.
enum class ControlMap {
  Absent,  ///< No segment with this name (ENOENT); nothing to attach.
  Full,    ///< This build's size, fully mapped: eligible for the trust gates.
  Foreign, ///< Another build's size: only the frozen header is mapped (if it even
           ///< fits). Never trusted -- guard the owner, then drop and recreate.
  Failed,  ///< Exists but is unusable (permissions, fstat or mmap failure).
};

struct ControlOpen {
  ControlMap       map     = ControlMap::Absent;
  CacheShmControl *ctrl    = nullptr; ///< nullptr when Absent/Failed, or Foreign and too small for the header.
  std::size_t      mapped  = 0;       ///< length to munmap.
  std::size_t      size    = 0;       ///< the segment's actual size, for the Foreign diagnostic.
  int              fd      = -1;      ///< open on Full/Foreign so the caller can flock it.
  int              sys_err = 0;       ///< errno behind Absent/Failed.
};

/// Not routed through open_and_map_shm: failing on a foreign size would leave the O_EXCL create below wedged on EEXIST
/// every restart until an operator ran `traffic_ctl cache shm clear`. Mapping just the frozen header instead keeps the
/// owner guard usable so the segment can be dropped.
ControlOpen
open_control_segment(const std::string &name)
{
  ControlOpen   out;
  ats_scoped_fd fd{shm_open(name.c_str(), O_RDWR, 0600)};
  if (fd < 0) {
    out.sys_err = errno;
    out.map     = out.sys_err == ENOENT ? ControlMap::Absent : ControlMap::Failed;
    Dbg(dbg_ctl, "shm_open(%s, open) failed: %s", name.c_str(), strerror(out.sys_err));
    return out;
  }

  // clang-format off
  struct stat sb{};
  // clang-format on
  if (fstat(fd, &sb) < 0) {
    out.sys_err = errno;
    out.map     = ControlMap::Failed;
    Warning("cache shm: fstat(%s) failed: %s", name.c_str(), strerror(out.sys_err));
    return out;
  }

  const std::size_t actual   = sb.st_size < 0 ? 0 : static_cast<std::size_t>(sb.st_size);
  const bool        own_size = cache_shm::is_own_control_size(actual);

  out.map    = own_size ? ControlMap::Full : ControlMap::Foreign;
  out.mapped = own_size ? CONTROL_SIZE : std::min(actual, CONTROL_HEADER_SIZE);
  out.size   = actual;

  if (out.mapped >= CONTROL_HEADER_SIZE) {
    void *addr = mmap(nullptr, out.mapped, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
      out.sys_err = errno;
      out.map     = ControlMap::Failed;
      Warning("mmap(%s, %zu) failed: %s", name.c_str(), out.mapped, strerror(out.sys_err));
      out.mapped = 0;
      return out;
    }
    out.ctrl = static_cast<CacheShmControl *>(addr);
  } else {
    // No ATS build wrote this, so there is nothing to read and the flock is the only owner guard left.
    out.mapped = 0;
  }

  out.fd = fd.release(); // caller owns it: flock, then close or keep for the run
  return out;
}

// `table` is nullptr when the stripe table may not be read (foreign layout or bad magic), which sweeps the name space
// instead so a stripe segment -- and the whole directory in it -- is never left behind.
void
unlink_all_known_segments(void *mapping, std::size_t mapping_len, const CacheShmControl *table)
{
  // Shares purge_segments()'s primitives so the prefix filter guarding them cannot drift. Cannot call purge_segments()
  // itself: this path already holds the control fd, mapping, and the exclusive lock that makes unlinking safe.
  std::vector<cache_shm::PurgeUnlink> unlinked;

  if (table != nullptr) {
    cache_shm::unlink_table_stripes(g_config.name_prefix, table, unlinked);
  } else {
    cache_shm::unlink_stripe_name_space(g_config.name_prefix, unlinked);
  }
  for (const auto &u : unlinked) {
    Dbg(dbg_ctl, "shm_unlink stripe %s%s", u.name.c_str(), table != nullptr ? "" : " (table untrusted; swept by name)");
  }

  if (mapping != nullptr) {
    munmap(mapping, mapping_len);
  }
  g_control = nullptr;
  if (!g_control_name.empty()) {
    Dbg(dbg_ctl, "shm_unlink control %s", g_control_name.c_str());
    shm_unlink(g_control_name.c_str());
  }
}

// Opt-in and best-effort: logs but never blocks startup. Shares the enumerate-and-unlink work with `traffic_ctl cache
// shm clear`; this only renders the result into diags.
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
  case cache_shm::PurgeOutcome::StatFailed:
    Warning("cache shm: cannot stat control segment %s to purge stale segments: %s", report.control_name.c_str(),
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

  if (report.table_untrusted) {
    Warning("cache shm: leftover control segment %s has an unreadable stripe table (%lld bytes); swept the '%ss<N>' name space",
            report.control_name.c_str(), report.segment_size, prefix.c_str());
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

  // Inherit the global hugepage intent unless the operator set the shm knob explicitly.
  RecSourceT hp_source = REC_SOURCE_NULL;
  if (!g_config.use_hugepages && ats_hugepage_enabled() &&
      RecGetRecordSource("proxy.config.cache.shm.use_hugepages", &hp_source) == REC_ERR_OKAY && hp_source == REC_SOURCE_DEFAULT) {
    g_config.use_hugepages = true;
  }

  RecInt purge_stale_on_start   = RecGetRecordInt("proxy.config.cache.shm.purge_stale_on_start").value_or(0);
  g_config.purge_stale_on_start = purge_stale_on_start != 0;

  char        prefix_buf[256] = {0};
  std::string configured      = "ats"; // operator sets only the middle word; framing is added below.
  if (RecGetRecordString("proxy.config.cache.shm.name_prefix", prefix_buf, sizeof(prefix_buf)).has_value() &&
      prefix_buf[0] != '\0') {
    configured = prefix_buf;
  }
  g_config.name_prefix = cache_shm::normalize_name_prefix(configured);

  return g_config.enabled;
}

// Marks the slot non-empty so a concurrent create cannot pick the same index. MAX_STRIPES when full. Caller must hold
// g_table_mutex.
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

  out_name = stripe_segment_name(g_config.name_prefix, idx);
  if (!reuse_slot) {
    g_control->stripe_count++;
  }
  StripeEntry &e = g_control->stripes[idx];
  ink_strlcpy(e.shm_name, out_name.c_str(), sizeof(e.shm_name));
  e.raw_dir_size    = directory_size;
  e.stripe_key_hash = key_hash;
  e.dir_untrusted   = 0; // fresh segment, so any mark from a prior occupant of this slot is stale
  return idx;
}

// Tombstones the slot for reuse. Caller must hold g_table_mutex.
void
release_reserved_slot(uint32_t idx)
{
  StripeEntry &e    = g_control->stripes[idx];
  e.shm_name[0]     = '\0';
  e.raw_dir_size    = 0;
  e.stripe_key_hash = 0;
  e.dir_untrusted   = 0;
}

// Takes the locks itself, so the shm syscalls that produced `p` could run with g_table_mutex dropped.
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
    g_pointers.insert({
      static_cast<char *>(p), MappedStripe{size, idx}
    });
  }
  return static_cast<char *>(p);
}

} // namespace

void
CacheShm::initialize(const Store &store)
{
  if (!load_config()) {
    _mode = Mode::Disabled;
    // Leftovers would keep consuming memory, and a later re-enable would attach a directory that went stale meanwhile.
    if (g_config.purge_stale_on_start) {
      purge_stale_segments(g_config.name_prefix);
    }
    Dbg(dbg_ctl, "shm disabled");
    return;
  }

  // Surface the MAP_HUGETLB -> THP substitution once so it isn't a silent downgrade.
  if (ats_hugepage_enabled()) {
    if (g_config.use_hugepages) {
      Note("cache shm: global hugepages enabled; MAP_HUGETLB is not usable for the tmpfs-backed dir, "
           "advising MADV_HUGEPAGE (transparent huge pages) on the mapping instead");
    } else {
      Warning("cache shm: global hugepages enabled but proxy.config.cache.shm.use_hugepages is 0; "
              "the tmpfs-backed dir will use base pages (MAP_HUGETLB does not apply to shm)");
    }
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
  ControlOpen opened = open_control_segment(g_control_name);
  if (opened.map == ControlMap::Failed) {
    Warning("cache shm: cannot use existing control segment %s: %s; shm disabled", g_control_name.c_str(),
            strerror(opened.sys_err));
    _mode = Mode::Disabled;
    return;
  }
  if (opened.map != ControlMap::Absent) {
    CacheShmControl *ctrl = opened.ctrl;

    // Refuse shm, and rebuild from disk, if another live process still owns this segment.
    int              flock_errno = 0;
    const LockResult lock        = try_lock_control(opened.fd, &flock_errno);
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
      live_owner = ctrl != nullptr && ctrl->owner_pid != 0 && ctrl->owner_pid != static_cast<int32_t>(getpid()) &&
                   process_is_alive(ctrl->owner_pid);
      break;
    }
    if (live_owner) {
      Warning("cache shm: control segment %s has a live owner (pid %d); disabling shm this run to avoid concurrent attach",
              g_control_name.c_str(), ctrl != nullptr ? ctrl->owner_pid : 0);
      if (ctrl != nullptr) {
        munmap(ctrl, opened.mapped);
      }
      close(opened.fd);
      _mode = Mode::Disabled;
      return;
    }

    // Only the frozen header was mapped; the stripe table behind it may have a different layout entirely.
    if (opened.map == ControlMap::Foreign) {
      Note("cache shm: control segment %s is %zu bytes, not this build's %zu; dropping it", g_control_name.c_str(), opened.size,
           CONTROL_SIZE);
    }
    const bool magic_ok = opened.map == ControlMap::Full && std::memcmp(ctrl->magic, CACHE_SHM_MAGIC, sizeof(CACHE_SHM_MAGIC)) == 0;

    bool ok = magic_ok;
    if (ok && ctrl->schema_version != CACHE_SHM_SCHEMA_VERSION) {
      Note("cache shm: schema mismatch (%u vs %u), dropping", ctrl->schema_version, CACHE_SHM_SCHEMA_VERSION);
      ok = false;
    }
    if (ok && ctrl->abi_hash != expected_abi) {
      Note("cache shm: ABI mismatch, dropping");
      ok = false;
    }

    // Not a hard gate: a storage change keeps the segment and each stripe attaches by its own identity.
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
      g_control_fd = opened.fd; // hold the exclusive lock for the process lifetime
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
    unlink_all_known_segments(ctrl, opened.mapped, magic_ok ? ctrl : nullptr);
    close(opened.fd); // releases the lock on the now-unlinked object
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

  // Decide under the table lock, then run the shm syscalls with it dropped (holding
  // it would serialize every disk thread's init; each stripe owns a distinct segment).
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
      if (g_entry_claimed[i]) {
        // Another stripe this run already took this entry, so two distinct stripes
        // hashed to one identity (duplicate hash_text, or a 64-bit FNV-1a collision).
        // Sharing one directory between them would corrupt both; create fresh instead.
        Warning("cache shm: stripe key collision on %s; creating a fresh segment for key=%s", read_shm_name(e.shm_name).c_str(),
                stripe_key);
        break;
      }
      if (e.dir_untrusted) {
        // Last shutdown could not vouch for this directory (bad disk, or a write still in flight). Tombstone it here rather
        // than leaving an orphan for finalize_attach: the slot is reused immediately, so stripe_count cannot creep across
        // runs that never reach finalize, and the old segment never coexists with its replacement.
        // The unlink stays under the lock even though the other shm syscalls do not: once the slot is a tombstone another
        // disk thread can reserve it and derive this same name, and unlinking after that would strip the name off the
        // segment it just created.
        const std::string name = read_shm_name(e.shm_name);
        Note("cache shm: stripe %s was marked untrusted at shutdown; recreating", name.c_str());
        shm_unlink(name.c_str());
        release_reserved_slot(i);
        break;
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
    release_reserved_slot(i);
    ++reclaimed;
  }
  if (reclaimed > 0) {
    Note("cache shm: reclaimed %u orphaned stripe segment(s) after attach", reclaimed);
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

void
CacheShm::invalidate_stripe_directory(char *raw_dir)
{
  uint32_t idx = MAX_STRIPES;
  {
    std::scoped_lock lk{g_pointers_mutex};
    auto             it = g_pointers.find(raw_dir);
    if (it == g_pointers.end()) {
      return; // not a shm-backed dir
    }
    idx = it->second.index;
  }

  // Marked in the control segment, never in the stripe's own header: that header aliases raw_dir, which both the shutdown
  // pwrite and the periodic dir sync copy to disk, so a mark there can clear the stripe on the next start instead of
  // rebuilding it. Next start, attach_or_create_stripe refuses the entry and creates a fresh segment, whose zero magic
  // sends Stripe::init down the disk-read path.
  std::scoped_lock lk{g_table_mutex};
  if (g_control == nullptr || idx >= MAX_STRIPES) {
    return;
  }
  g_control->stripes[idx].dir_untrusted = 1;
  msync(g_control, CONTROL_SIZE, MS_SYNC);
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
  munmap(it->first, it->second.size);
  g_pointers.erase(it);
}

void
CacheShm::release_for_test()
{
  std::scoped_lock lk{g_table_mutex};
  if (g_control != nullptr) {
    munmap(g_control, CONTROL_SIZE);
    g_control = nullptr;
  }
  // Closing the fd is what releases the control flock, so the next initialize() in this process is not refused as a
  // concurrent attach.
  g_control_fd = ats_scoped_fd{};
  g_control_name.clear();
  std::memset(g_entry_claimed, 0, sizeof(g_entry_claimed));
  g_claims_this_run = 0;
  _mode             = Mode::Disabled;
}

#endif // TS_USE_CACHE_SHM
