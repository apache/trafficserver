/** @file

  traffic_ctl command for inspecting and clearing the cache shared-memory
  control segment and its associated stripe segments.

  The status and clear operations work by direct shm_open access rather than
  JSONRPC, so they function whether traffic_server is running or not. This
  is important for debugging crash-leftover segments when no live process
  is available to query.

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

#include "CacheShmCommand.h"
#include "shared/cache_shm/Layout.h"
#include "shared/cache_shm/Purge.h"
#include "TrafficCtlStatus.h"

#include "tscore/ink_config.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace
{

// Must match the proxy.config.cache.shm.name_prefix default, or this tool inspects a different segment than the server made.
constexpr const char *DEFAULT_PREFIX = "ats";

#if TS_USE_CACHE_SHM

bool
shm_segment_exists(const std::string &name)
{
  int fd = shm_open(name.c_str(), O_RDONLY, 0);
  if (fd < 0) {
    return false;
  }
  close(fd);
  return true;
}

std::string
format_size(uint64_t bytes)
{
  char buf[64];
  if (bytes >= (uint64_t{1} << 30)) {
    std::snprintf(buf, sizeof(buf), "%.2f GiB", static_cast<double>(bytes) / (uint64_t{1} << 30));
  } else if (bytes >= (uint64_t{1} << 20)) {
    std::snprintf(buf, sizeof(buf), "%.2f MiB", static_cast<double>(bytes) / (uint64_t{1} << 20));
  } else if (bytes >= (uint64_t{1} << 10)) {
    std::snprintf(buf, sizeof(buf), "%.2f KiB", static_cast<double>(bytes) / (uint64_t{1} << 10));
  } else {
    std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
  }
  return buf;
}

using cache_shm::process_is_alive;
using cache_shm::read_shm_name;

#endif // TS_USE_CACHE_SHM

} // namespace

CacheShmCommand::CacheShmCommand(ts::Arguments *args) : CtrlCommand(args)
{
  if (get_parsed_arguments()->get(STATUS_STR)) {
    _invoked_func = [this]() { status(); };
  } else if (get_parsed_arguments()->get(CLEAR_STR)) {
    _invoked_func = [this]() { clear(); };
  }
}

std::string
CacheShmCommand::get_prefix()
{
  // Framed the same way the server does, so the two agree on segment names.
  std::string configured = DEFAULT_PREFIX;
  if (auto arg = get_parsed_arguments()->get(PREFIX_STR); arg && !arg.empty()) {
    configured = arg.value();
  }
  return cache_shm::normalize_name_prefix(configured);
}

// A miss against the default prefix usually means name_prefix is set and was not repeated here; say so rather than leave
// the operator concluding shm was never enabled.
std::string
CacheShmCommand::default_prefix_hint()
{
  if (auto arg = get_parsed_arguments()->get(PREFIX_STR); arg && !arg.empty()) {
    return {};
  }
  return " (if proxy.config.cache.shm.name_prefix is set, pass --prefix <word>)";
}

void
CacheShmCommand::report_unsupported()
{
  std::cerr << "cache shm: this build has no POSIX shared memory support, so the cache shm "
               "fast-restart feature is compiled out; there is nothing to inspect or clear.\n";
  App_Exit_Status_Code = CTRL_EX_UNIMPLEMENTED;
}

void
CacheShmCommand::status()
{
#if !TS_USE_CACHE_SHM
  report_unsupported();
#else
  const std::string prefix       = get_prefix();
  const std::string control_name = cache_shm::control_segment_name(prefix);

  int fd = shm_open(control_name.c_str(), O_RDONLY, 0);
  if (fd < 0) {
    std::cerr << "cache shm: control segment '" << control_name << "' not found: " << std::strerror(errno) << default_prefix_hint()
              << '\n';
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  }

  struct stat sb {
  };
  if (fstat(fd, &sb) < 0) {
    std::cerr << "cache shm: fstat failed: " << std::strerror(errno) << '\n';
    close(fd);
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  }

  if (!cache_shm::is_own_control_size(static_cast<std::size_t>(sb.st_size))) {
    // stripes[] may have another stride, so interpreting it with our layout would print nonsense. `clear` refuses the
    // same segment for the same reason.
    std::cerr << "cache shm: control segment '" << control_name << "' is " << sb.st_size << " bytes, not this build's "
              << cache_shm::CONTROL_SIZE << "; it was written by a different build and cannot be interpreted.\n";
    close(fd);
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  }

  void *addr = mmap(nullptr, cache_shm::CONTROL_SIZE, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if (addr == MAP_FAILED) {
    std::cerr << "cache shm: mmap failed: " << std::strerror(errno) << '\n';
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  }

  const auto *ctrl = static_cast<const cache_shm::CacheShmControl *>(addr);

  const bool magic_ok  = std::memcmp(ctrl->magic, cache_shm::CACHE_SHM_MAGIC, sizeof(cache_shm::CACHE_SHM_MAGIC)) == 0;
  const bool schema_ok = ctrl->schema_version == cache_shm::CACHE_SHM_SCHEMA_VERSION;

  std::cout << "Control segment:    " << control_name << '\n';
  std::cout << "  segment size:     " << sb.st_size << " bytes (" << format_size(sb.st_size) << ")\n";
  std::cout << "  magic:            ";
  for (char c : ctrl->magic) {
    if (c >= 0x20 && c < 0x7f) {
      std::cout << c;
    }
  }
  std::cout << (magic_ok ? "  [valid]" : "  [INVALID]") << '\n';
  std::cout << "  schema_version:   " << ctrl->schema_version << (schema_ok ? "  [valid]" : "  [INVALID]") << '\n';
  std::cout << "  abi_hash:         0x" << std::hex << ctrl->abi_hash << std::dec << '\n';
  std::cout << "  storage_sig:      0x" << std::hex << ctrl->storage_signature << std::dec << '\n';
  std::cout << "  clean_shutdown:   " << static_cast<int>(ctrl->clean_shutdown)
            << (ctrl->clean_shutdown ? " (clean)" : " (DIRTY -- next start will rebuild)") << '\n';
  std::cout << "  owner_pid:        " << ctrl->owner_pid;
  if (ctrl->owner_pid == 0) {
    std::cout << " (none -- not currently attached)";
  } else if (process_is_alive(ctrl->owner_pid)) {
    std::cout << " (LIVE -- a running traffic_server owns this segment)";
  } else {
    std::cout << " (stale -- owner no longer running)";
  }
  std::cout << '\n';
  std::cout << "  stripe_count:     " << ctrl->stripe_count << '\n';

  if (!magic_ok || !schema_ok) {
    std::cout << "\nHeader is invalid; not interpreting stripe table.\n";
    munmap(addr, cache_shm::CONTROL_SIZE);
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  }

  const uint32_t shown = std::min<uint32_t>(ctrl->stripe_count, cache_shm::MAX_STRIPES);

  if (shown > 0) {
    std::cout << "\nStripes:\n";
    for (uint32_t i = 0; i < shown; ++i) {
      const auto &entry = ctrl->stripes[i];
      std::string name  = read_shm_name(entry.shm_name);
      if (name.empty()) {
        std::cout << "  [" << i << "] (tombstone -- slot free for reuse)\n";
        continue;
      }
      const bool present = shm_segment_exists(name);
      std::cout << "  [" << i << "] " << name << "  size=" << entry.raw_dir_size << " (" << format_size(entry.raw_dir_size) << ")  "
                << (present ? "present" : "MISSING") << (entry.dir_untrusted ? "  untrusted (will rebuild from disk)" : "") << '\n';
    }
  }

  if (ctrl->stripe_count > cache_shm::MAX_STRIPES) {
    std::cout << "\n(stripe_count " << ctrl->stripe_count << " exceeds MAX_STRIPES " << cache_shm::MAX_STRIPES << "; truncated.)\n";
  }

  munmap(addr, cache_shm::CONTROL_SIZE);
#endif // TS_USE_CACHE_SHM
}

void
CacheShmCommand::clear()
{
#if !TS_USE_CACHE_SHM
  report_unsupported();
#else
  // Shared with the server's purge-on-disabled-start; this only renders the result and sets the exit code.
  const cache_shm::PurgeReport report = cache_shm::purge_segments(get_prefix());

  switch (report.outcome) {
  case cache_shm::PurgeOutcome::BadPrefix:
    std::cerr << "cache shm: invalid prefix (must be non-empty and begin with '/').\n";
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  case cache_shm::PurgeOutcome::NotPresent:
    std::cerr << "cache shm: control segment '" << report.control_name << "' not found (" << std::strerror(report.sys_errno) << ")"
              << default_prefix_hint() << "; nothing to clear.\n";
    std::cout << "Removed 0 segment(s).\n";
    return;
  case cache_shm::PurgeOutcome::OpenFailed:
    // Not ENOENT: the segment may well exist but we could not open it (e.g. EACCES on a
    // segment owned by another user). Report the real errno and fail rather than claim success.
    std::cerr << "cache shm: cannot open control segment '" << report.control_name << "' (" << std::strerror(report.sys_errno)
              << "); cannot clear.\n";
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  case cache_shm::PurgeOutcome::MapFailed:
    std::cerr << "cache shm: mmap failed while reading stripe table: " << std::strerror(report.sys_errno) << '\n';
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  case cache_shm::PurgeOutcome::StatFailed:
    std::cerr << "cache shm: cannot stat control segment '" << report.control_name << "' (" << std::strerror(report.sys_errno)
              << "); cannot clear.\n";
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  case cache_shm::PurgeOutcome::OwnedByLive:
    // Refuse: unlinking a live owner's segments would orphan its fast restart.
    std::cerr << "cache shm: control segment '" << report.control_name << "' is owned by a live traffic_server (pid "
              << report.owner_pid << "); refusing to clear. Stop traffic_server first.\n";
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  case cache_shm::PurgeOutcome::TooSmall:
  case cache_shm::PurgeOutcome::Purged:
    break;
  }

  if (report.table_untrusted) {
    std::cerr << "cache shm: control segment '" << report.control_name << "' (" << report.segment_size
              << " bytes) was written by a different build; its stripe table could not be read, so every '" << get_prefix()
              << "s<N>' name was swept instead.\n";
  }

  for (const auto &u : report.unlinked) {
    if (u.error == 0) {
      std::cout << "unlinked " << u.name << '\n';
    } else if (u.error != ENOENT) {
      std::cerr << "failed to unlink " << u.name << ": " << std::strerror(u.error) << '\n';
    }
  }

  const unsigned failures = report.failures();
  std::cout << "Removed " << report.removed() << " segment(s)";
  if (failures != 0) {
    std::cout << ", " << failures << " failure(s)";
    App_Exit_Status_Code = CTRL_EX_ERROR;
  }
  std::cout << ".\n";
#endif // TS_USE_CACHE_SHM
}
