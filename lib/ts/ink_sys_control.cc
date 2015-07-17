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

#include "ts/ink_defs.h"
#include "ts/ink_assert.h"
#include "ts/ink_sys_control.h"

rlim_t
ink_max_out_rlimit(int which, bool max_it, bool unlim_it)
{
  struct rlimit rl;

#if defined(linux)
#define MAGIC_CAST(x) (enum __rlimit_resource)(x)
#else
#define MAGIC_CAST(x) x
#endif

  if (max_it) {
    ink_release_assert(getrlimit(MAGIC_CAST(which), &rl) >= 0);
    if (rl.rlim_cur != rl.rlim_max) {
#if defined(darwin)
      if (which == RLIMIT_NOFILE)
        rl.rlim_cur = fmin(OPEN_MAX, rl.rlim_max);
      else
        rl.rlim_cur = rl.rlim_max;
#else
      rl.rlim_cur = rl.rlim_max;
#endif
      ink_release_assert(setrlimit(MAGIC_CAST(which), &rl) >= 0);
    }
  }

#if !(defined(darwin) || defined(freebsd))
  if (unlim_it) {
    ink_release_assert(getrlimit(MAGIC_CAST(which), &rl) >= 0);
    if (rl.rlim_cur != (rlim_t)RLIM_INFINITY) {
      rl.rlim_cur = (rl.rlim_max = RLIM_INFINITY);
      ink_release_assert(setrlimit(MAGIC_CAST(which), &rl) >= 0);
    }
  }
#endif
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
