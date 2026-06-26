/** @file

  Shared "enumerate and unlink the shm segments for a prefix" primitive, used by
  both the cache subsystem (purge-on-disabled-start) and `traffic_ctl cache shm
  clear`. Header-only since traffic_ctl does not link the cache library.
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

#include "CacheShmLayout.h"

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

/// True if `pid` names a live process (pid <= 0 is not). EPERM counts as alive (it
/// exists, we just may not signal it). Backs the owner-liveness backstop used where
/// the control-segment flock is not honored.
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

/// Take the exclusive, non-blocking advisory lock on the control fd. Authoritative
/// on Linux/tmpfs (auto-released on crash); macOS POSIX shm returns Unsupported, so
/// the owner_pid liveness check is used there instead. On Unsupported the flock errno
/// is reported via `unexpected_errno` (when non-null) so a caller with logging can
/// surface an otherwise-silent failure (EBADF/EINVAL/ENOLCK vs the expected macOS case).
inline LockResult
try_lock_control(int fd, int *unexpected_errno = nullptr)
{
  if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
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

/// Read a shm_name field bounded by the field size (the fixed char[] may be
/// un-terminated in a tampered/stale segment). Empty for a tombstoned slot.
inline std::string
read_shm_name(const char (&field)[32])
{
  return std::string(field, ::strnlen(field, sizeof(field)));
}

/// How far purge_segments() got. Everything but Purged/TooSmall means nothing was
/// unlinked.
enum class PurgeOutcome {
  BadPrefix,   ///< Prefix is empty or does not start with '/'. Nothing attempted.
  NotPresent,  ///< No <prefix>control segment exists (shm_open ENOENT). Nothing to do.
  OpenFailed,  ///< shm_open failed for a reason other than ENOENT; cannot read safely.
  MapFailed,   ///< The control segment exists but could not be mmap'd.
  TooSmall,    ///< Control segment is smaller than CacheShmControl; control unlinked, table not walked.
  OwnedByLive, ///< A live process owns the segment; nothing was unlinked.
  Purged,      ///< The stripe table was walked and its segments unlinked (possibly zero stripes).
};

/// One shm_unlink attempt, so callers can log each name in their own format.
struct PurgeUnlink {
  std::string name;
  bool        is_control; ///< true for the <prefix>control object, false for a stripe.
  int         error;      ///< 0 on success; otherwise the errno from shm_unlink (ENOENT == already gone).
};

/// Result of purge_segments(). `unlinked` lists every shm_unlink attempted, in
/// order (stripes first, then the control object).
struct PurgeReport {
  PurgeOutcome             outcome = PurgeOutcome::NotPresent;
  std::string              control_name;      ///< the <prefix>control name (set whenever the prefix was valid).
  int                      sys_errno    = 0;  ///< errno behind OpenFailed / MapFailed.
  long long                segment_size = -1; ///< control segment size in bytes, for TooSmall.
  int32_t                  owner_pid    = 0;  ///< the recorded owner pid, for OwnedByLive.
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

  /// Real failures. ENOENT means the segment was already gone, which is the
  /// desired end state, so it is not counted.
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

/// Open `<prefix>control` read-only and, unless a live process still owns it,
/// unlink every stripe segment it lists plus the control object. No logging --
/// callers format the returned report. The owner guard uses flock, falling back to
/// owner_pid liveness where flock is unsupported. The stripe table is trusted on
/// magic alone (the size check bounds the read; stale names just ENOENT on unlink).
inline PurgeReport
purge_segments(const std::string &prefix)
{
  PurgeReport report;

  if (prefix.empty() || prefix[0] != '/') {
    report.outcome = PurgeOutcome::BadPrefix;
    return report;
  }
  report.control_name = control_segment_name(prefix);

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
  if (::fstat(fd, &sb) < 0 || static_cast<std::size_t>(sb.st_size) < CONTROL_SIZE) {
    // Too small to hold a valid header/table: there is no table to walk, so just
    // unlink the control object itself (it still occupies memory).
    report.segment_size = static_cast<long long>(sb.st_size);
    report.outcome      = PurgeOutcome::TooSmall;
    int e               = ::shm_unlink(report.control_name.c_str()) == 0 ? 0 : errno;
    report.unlinked.push_back({report.control_name, true, e});
    return report;
  }

  void *addr = ::mmap(nullptr, CONTROL_SIZE, PROT_READ, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    report.sys_errno = errno;
    report.outcome   = PurgeOutcome::MapFailed;
    return report;
  }

  const auto *ctrl     = static_cast<const CacheShmControl *>(addr);
  const bool  magic_ok = std::memcmp(ctrl->magic, CACHE_SHM_MAGIC, sizeof(CACHE_SHM_MAGIC)) == 0;

  const LockResult lock = try_lock_control(fd);
  if (lock == LockResult::HeldByOther || (lock == LockResult::Unsupported && magic_ok && process_is_alive(ctrl->owner_pid))) {
    report.owner_pid = magic_ok ? ctrl->owner_pid : 0;
    report.outcome   = PurgeOutcome::OwnedByLive;
    ::munmap(addr, CONTROL_SIZE);
    return report;
  }

  const uint32_t stripe_count = magic_ok ? std::min<uint32_t>(ctrl->stripe_count, MAX_STRIPES) : 0;
  for (uint32_t i = 0; i < stripe_count; ++i) {
    std::string name = read_shm_name(ctrl->stripes[i].shm_name);
    if (name.empty()) {
      continue; // tombstoned slot -- nothing to unlink
    }
    int e = ::shm_unlink(name.c_str()) == 0 ? 0 : errno;
    report.unlinked.push_back({std::move(name), false, e});
  }
  ::munmap(addr, CONTROL_SIZE);

  int e = ::shm_unlink(report.control_name.c_str()) == 0 ? 0 : errno;
  report.unlinked.push_back({report.control_name, true, e});

  report.outcome = PurgeOutcome::Purged;
  return report;
}

} // namespace cache_shm
