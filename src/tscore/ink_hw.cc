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
#if TS_USE_HWLOC
#if HAVE_HWLOC_OBJ_PU
  return hwloc_get_nbobjs_by_type(ink_get_topology(), HWLOC_OBJ_PU);
#else
  return hwloc_get_nbobjs_by_type(ink_get_topology(), HWLOC_OBJ_CORE);
#endif
#elif defined(freebsd)
  int mib[2], n;
  mib[0]     = CTL_HW;
  mib[1]     = HW_NCPU;
  size_t len = sizeof(n);
  if (sysctl(mib, 2, &n, &len, nullptr, 0) == -1)
    return 1;
  return n;
#else
  return sysconf(_SC_NPROCESSORS_ONLN); // number of processing units (includes Hyper Threading)
#endif
}
