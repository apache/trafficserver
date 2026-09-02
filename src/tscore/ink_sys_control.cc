/** @file

  This file produces Unicorns and Rainbows for the ATS community

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

#include <cmath>
#include <cstdio>
#include <unistd.h>

#include "tscore/ink_defs.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_sys_control.h"
#include "tscore/Diags.h"

#if defined(darwin)
#include <mach/mach.h>
#endif

#if defined(freebsd)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

namespace
{
rlim_t global_fds_limit = 8000;
}

rlim_t
ink_get_fds_limit()
{
  return global_fds_limit;
}

void
ink_set_fds_limit(rlim_t limit)
{
  global_fds_limit = limit;
}

rlim_t
ink_max_out_rlimit(int which)
{
  struct rlimit rl;

#if defined(__GLIBC__)
#define MAGIC_CAST(x) (enum __rlimit_resource)(x)
#else
#define MAGIC_CAST(x) x
#endif

  ink_release_assert(getrlimit(MAGIC_CAST(which), &rl) >= 0);
  if (rl.rlim_cur != rl.rlim_max) {
#if defined(darwin)
    if (which == RLIMIT_NOFILE) {
      rl.rlim_cur = (OPEN_MAX < rl.rlim_max) ? OPEN_MAX : rl.rlim_max;
    } else {
      rl.rlim_cur = rl.rlim_max;
    }
#else
    rl.rlim_cur = rl.rlim_max;
#endif
    if (setrlimit(MAGIC_CAST(which), &rl) != 0) {
      Warning("Failed to set Limit : %s", strerror(errno));
    }
  }
  ink_release_assert(getrlimit(MAGIC_CAST(which), &rl) >= 0);
  return rl.rlim_cur;
}

rlim_t
ink_get_max_files()
{
  FILE         *fd;
  struct rlimit lim;

  // Linux-only ...
  if ((fd = fopen("/proc/sys/fs/file-max", "r"))) {
    uint64_t fmax;
    if (fscanf(fd, "%" PRIu64 "", &fmax) == 1) {
      fclose(fd);
      return static_cast<rlim_t>(fmax);
    }

    fclose(fd);
  }

  if (getrlimit(RLIMIT_NOFILE, &lim) == 0) {
    return lim.rlim_max;
  }

  return RLIM_INFINITY;
}

uint64_t
ink_get_current_rss()
{
#if defined(__linux__)
  // /proc/self/statm reports sizes in pages. The second field is the resident
  // set size (number of resident pages).
  FILE *fp = fopen("/proc/self/statm", "r");
  if (fp == nullptr) {
    return 0;
  }

  unsigned long total_pages    = 0;
  unsigned long resident_pages = 0;
  int           matched        = fscanf(fp, "%lu %lu", &total_pages, &resident_pages);
  fclose(fp);

  if (matched != 2) {
    return 0;
  }

  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    return 0;
  }

  return static_cast<uint64_t>(resident_pages) * static_cast<uint64_t>(page_size);
#elif defined(darwin)
  // On macOS, task_info() reports resident_size in bytes.
  mach_task_basic_info_data_t info;
  mach_msg_type_number_t      count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0;
  }

  return static_cast<uint64_t>(info.resident_size);
#elif defined(freebsd)
  // On FreeBSD, kinfo_proc.ki_rssize is the resident set size in pages.
  struct kinfo_proc kp;
  size_t            len   = sizeof(kp);
  int               mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), &kp, &len, nullptr, 0) != 0 || len != sizeof(kp)) {
    return 0;
  }

  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    return 0;
  }

  return static_cast<uint64_t>(kp.ki_rssize) * static_cast<uint64_t>(page_size);
#else
  return 0;
#endif
}
