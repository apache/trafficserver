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

#include "tscore/ink_defs.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_sys_control.h"
#include "tscore/Diags.h"

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
#if (defined(__APPLE__) && defined(__MACH__))
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
  FILE *fd;
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
