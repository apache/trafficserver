/** @file

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

#include "tscore/ink_hw.h"
#include "tscore/ink_platform.h"
#include "tsutil/DbgCtl.h"

static DbgCtl dbg_ctl_verbose_threads{"v_threads"};

#if TS_USE_HWLOC

#include <hwloc.h>

// Little helper to initialize the hwloc topology, once.
static hwloc_topology_t
setup_hwloc()
{
  hwloc_topology_t topology;

  hwloc_topology_init(&topology);
  hwloc_topology_load(topology);

  return topology;
}

// Get the topology
hwloc_topology_t
ink_get_topology()
{
  static hwloc_topology_t topology = setup_hwloc();
  return topology;
}
#endif

int
ink_number_of_processors()
{
  int number_of_processors = 0;

#if TS_USE_HWLOC
#if HAVE_HWLOC_OBJ_PU
  number_of_processors = hwloc_get_nbobjs_by_type(ink_get_topology(), HWLOC_OBJ_PU);
  Dbg(dbg_ctl_verbose_threads, "processing unit count from hwloc: %d", number_of_processors);
#else
  number_of_processors = hwloc_get_nbobjs_by_type(ink_get_topology(), HWLOC_OBJ_CORE);
  Dbg(dbg_ctl_verbose_threads, "core count from hwloc: %d", number_of_processors);
#endif
#elif defined(freebsd)
  int mib[2];
  mib[0]     = CTL_HW;
  mib[1]     = HW_NCPU;
  size_t len = sizeof(number_of_processors);
  if (sysctl(mib, 2, &number_of_processors, &len, nullptr, 0) == -1) {
    Dbg(dbg_ctl_verbose_threads, "sysctl failed: %s", strerror(errno));
    return 1;
  }
  Dbg(dbg_ctl_verbose_threads, "processing unit count from sysctl: %d", number_of_processors);
#else
  number_of_processors = sysconf(_SC_NPROCESSORS_ONLN); // number of processing units (includes Hyper Threading)
  Dbg(dbg_ctl_verbose_threads, "processing unit count from sysconf: %d", number_of_processors);
#endif

  return number_of_processors;
}
