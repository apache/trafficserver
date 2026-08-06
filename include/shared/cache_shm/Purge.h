/** @file

  Shared "enumerate and unlink the shm segments for a prefix" primitive, used by
  both the cache subsystem (purge-on-disabled-start) and `traffic_ctl cache shm
  clear`. Header-only since traffic_ctl does not link the cache library; tscore is
  fine here, both consumers link it.
  purge_segments() does no logging; it returns a report each caller formats itself.

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

#include "shared/cache_shm/Layout.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace cache_shm
{

/// True if `pid` names a live process. EPERM counts as alive: it exists, we just may not signal it.
inline bool
process_is_alive(int32_t pid)
{
  if (pid <= 0) {
    return false;
  }
  return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

/// Outcome of trying to take the control segment's exclusive lock.
enum class LockResult {
  Acquired,    ///< We hold the exclusive lock; no other process does.
  HeldByOther, ///< Another live process holds it (flock returned EWOULDBLOCK).
  Unsupported, ///< flock is not honored for this fd (e.g. macOS POSIX shm).
};

/// Authoritative on Linux/tmpfs, where the lock is auto-released on crash. macOS POSIX shm returns Unsupported, so
/// callers fall back to owner_pid liveness; `unexpected_errno` distinguishes that expected case from EBADF/EINVAL/ENOLCK.
inline LockResult
try_lock_control(int fd, int *unexpected_errno = nullptr)
{
  int rc = 0;
  while ((rc = ::flock(fd, LOCK_EX | LOCK_NB)) != 0 && errno == EINTR) {
    ; // retry: a signal, not a real lock failure
  }
  if (rc == 0) {
    return LockResult::Acquired;
  }
  // EWOULDBLOCK is the only errno meaning "another process holds it"; anything else
  // means flock is unusable here -> fall back to the owner_pid backstop.
  if (errno == EWOULDBLOCK) {
    return LockResult::HeldByOther;
  }
  if (unexpected_errno != nullptr) {
    *unexpected_errno = errno;
  }
  return LockResult::Unsupported;
}

/// Bounded by the field size: the fixed char[] may be un-terminated in a tampered or stale segment.
inline std::string
read_shm_name(const char (&field)[MAX_SHM_NAME_LEN + 1])
{
  return std::string(field, ::strnlen(field, sizeof(field)));
}

/// Derived from the control-table index so the purge path can sweep the whole name space without a trustworthy table.
inline std::string
stripe_segment_name(const std::string &prefix, uint32_t stripe_index)
{
  std::string name = prefix + "s" + std::to_string(stripe_index);
  if (name.size() >= MAX_SHM_NAME_LEN) {
    name.resize(MAX_SHM_NAME_LEN - 1);
  }
  return name;
}

/// Everything but Purged/TooSmall means nothing was unlinked.
enum class PurgeOutcome {
  BadPrefix,   ///< Prefix is empty or does not start with '/'. Nothing attempted.
  NotPresent,  ///< No <prefix>control segment exists (shm_open ENOENT). Nothing to do.
  OpenFailed,  ///< shm_open failed for a reason other than ENOENT; cannot read safely.
  MapFailed,   ///< The control segment exists but could not be mmap'd.
  StatFailed,  ///< fstat on the control fd failed; size/validity unknown, nothing unlinked.
  TooSmall,    ///< Control segment is smaller than CacheShmControl; table not walked, name space swept instead.
  OwnedByLive, ///< A live process owns the segment; nothing was unlinked.
  Purged,      ///< The stripe table was walked and its segments unlinked (possibly zero stripes).
};

/// One shm_unlink attempt, so callers can log each name in their own format.
struct PurgeUnlink {
  std::string name;
  bool        is_control; ///< true for the <prefix>control object, false for a stripe.
  int         error;      ///< 0 on success; otherwise the errno from shm_unlink (ENOENT == already gone).
};

/// `unlinked` lists every shm_unlink attempted, stripes first, then the control object.
struct PurgeReport {
  PurgeOutcome             outcome = PurgeOutcome::NotPresent;
  std::string              control_name;            ///< the <prefix>control name (set whenever the prefix was valid).
  int                      sys_errno       = 0;     ///< errno behind OpenFailed / MapFailed.
  long long                segment_size    = -1;    ///< control segment size in bytes; set whenever fstat succeeded.
  int32_t                  owner_pid       = 0;     ///< the recorded owner pid, for OwnedByLive.
  bool                     table_untrusted = false; ///< the stripe table could not be walked; swept `<prefix>s<N>` by name instead.
  std::vector<PurgeUnlink> unlinked;

  /// Segments successfully removed (a shm_unlink that returned 0).
  unsigned
  removed() const
  {
    unsigned n = 0;
    for (const auto &u : unlinked) {
      if (u.error == 0) {
        ++n;
      }
    }
    return n;
  }

  /// ENOENT is not counted: the segment being already gone is the desired end state.
  unsigned
  failures() const
  {
    unsigned n = 0;
    for (const auto &u : unlinked) {
      if (u.error != 0 && u.error != ENOENT) {
        ++n;
      }
    }
    return n;
  }
};

namespace detail
{
  /// Close an fd on scope exit (the mmap survives the close).
  struct FdGuard {
    int fd;
    ~FdGuard()
    {
      if (fd >= 0) {
        ::close(fd);
      }
    }
  };
} // namespace detail

/// Only names under `prefix` are touched, so a corrupt but magic-valid table cannot drive shm_unlink on unrelated objects.
inline void
unlink_table_stripes(const std::string &prefix, const CacheShmControl *table, std::vector<PurgeUnlink> &out)
{
  const uint32_t stripe_count = std::min<uint32_t>(table->stripe_count, MAX_STRIPES);

  for (uint32_t i = 0; i < stripe_count; ++i) {
    std::string name = read_shm_name(table->stripes[i].shm_name);
    if (name.empty() || !name.starts_with(prefix)) {
      continue;
    }
    int e = ::shm_unlink(name.c_str()) == 0 ? 0 : errno;
    out.push_back({std::move(name), false, e});
  }
}

/// Fallback for an unreadable stripe table: the names are ours by construction, and leaving a stripe segment behind would
/// leak the whole directory in it. Absent indices just ENOENT.
inline void
unlink_stripe_name_space(const std::string &prefix, std::vector<PurgeUnlink> &out)
{
  for (uint32_t i = 0; i < MAX_STRIPES; ++i) {
    std::string name = stripe_segment_name(prefix, i);
    if (::shm_unlink(name.c_str()) == 0) {
      out.push_back({std::move(name), false, 0});
    }
  }
}

/// Unlinks the stripe segments plus the control object, unless a live process still owns it. No logging -- callers format
/// the report. The stripe table is trusted only when the magic matches and the size is ours; otherwise the name space is
/// swept instead (report.table_untrusted). See "Operator tooling" in the shm-fast-restart developer guide.
inline PurgeReport
purge_segments(const std::string &prefix)
{
  PurgeReport report;

  if (prefix.empty() || prefix[0] != '/') {
    report.outcome = PurgeOutcome::BadPrefix;
    return report;
  }
  report.control_name = control_segment_name(prefix);

  // Records the name-sweep fallback where the fact is established, so the flag cannot
  // disagree with the outcome that led to it.
  auto sweep_stripe_name_space = [&report, &prefix]() {
    report.table_untrusted = true;
    unlink_stripe_name_space(prefix, report.unlinked);
  };

  int fd = ::shm_open(report.control_name.c_str(), O_RDONLY, 0);
  if (fd < 0) {
    report.sys_errno = errno;
    report.outcome   = (errno == ENOENT) ? PurgeOutcome::NotPresent : PurgeOutcome::OpenFailed;
    return report;
  }
  detail::FdGuard guard{fd};

  // clang-format off
  struct stat sb{};
  // clang-format on
  if (::fstat(fd, &sb) < 0) {
    // Size and validity are unknown: report the error and leave the segment
    // alone rather than unlink a control object we failed to stat.
    report.sys_errno = errno;
    report.outcome   = PurgeOutcome::StatFailed;
    return report;
  }
  report.segment_size = static_cast<long long>(sb.st_size);

  // Before any size branch: flock needs only the fd, and a segment too small for *our* layout can still be a live older
  // build's -- unlinking it would strip the names out from under the very upgrade the frozen header exists to support.
  const LockResult lock = try_lock_control(fd);

  // Map only what is really there. A foreign build's segment may be shorter than CONTROL_SIZE, and mapping past the last
  // page of the object faults on access; the frozen header prefix is all the owner guard needs.
  const std::size_t map_len = std::min(static_cast<std::size_t>(sb.st_size), CONTROL_SIZE);
  void             *addr    = nullptr;
  if (map_len >= CONTROL_HEADER_SIZE) {
    addr = ::mmap(nullptr, map_len, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
      report.sys_errno = errno;
      report.outcome   = PurgeOutcome::MapFailed;
      return report;
    }
  }
  const auto *ctrl = static_cast<const CacheShmControl *>(addr);
  // Too short to even hold the frozen header: no build of ours wrote it, so there is no owner to protect.
  const bool magic_ok = ctrl != nullptr && std::memcmp(ctrl->magic, CACHE_SHM_MAGIC, sizeof(CACHE_SHM_MAGIC)) == 0;

  if (lock == LockResult::HeldByOther || (lock == LockResult::Unsupported && magic_ok && process_is_alive(ctrl->owner_pid))) {
    report.owner_pid = magic_ok ? ctrl->owner_pid : 0;
    report.outcome   = PurgeOutcome::OwnedByLive;
    if (addr != nullptr) {
      ::munmap(addr, map_len);
    }
    return report;
  }

  if (static_cast<std::size_t>(sb.st_size) < CONTROL_SIZE) {
    // Too small to hold this build's header/table: there is no table to walk, so
    // sweep the stripe name space and unlink the control object itself.
    report.outcome = PurgeOutcome::TooSmall;
    sweep_stripe_name_space();
    int e = ::shm_unlink(report.control_name.c_str()) == 0 ? 0 : errno;
    report.unlinked.push_back({report.control_name, true, e});
    if (addr != nullptr) {
      ::munmap(addr, map_len);
    }
    return report;
  }

  // Larger than this build's page-rounded CONTROL_SIZE means a build with a different
  // sizeof(CacheShmControl) wrote it. The frozen header prefix is still readable (so
  // the owner guard above applies), but stripes[] may have a different stride entirely,
  // so its names must not drive shm_unlink.
  if (magic_ok && is_own_control_size(static_cast<std::size_t>(sb.st_size))) {
    unlink_table_stripes(prefix, ctrl, report.unlinked);
  } else {
    // Bad magic, or a foreign sizeof(CacheShmControl): the table cannot be walked.
    sweep_stripe_name_space();
  }
  ::munmap(addr, map_len);

  int e = ::shm_unlink(report.control_name.c_str()) == 0 ? 0 : errno;
  report.unlinked.push_back({report.control_name, true, e});

  report.outcome = PurgeOutcome::Purged;
  return report;
}

} // namespace cache_shm
