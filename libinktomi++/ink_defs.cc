/** @file

  A brief file description

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

/****************************************************************************

  ink_defs.h
  Some small general interest definitions

 ****************************************************************************/

#include "inktomi++.h"
#include "ink_unused.h"
#include "ink_platform.h"
#if (HOST_OS == linux) || (HOST_OS == freebsd) || (HOST_OS == darwin)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
#if (HOST_OS == linux)
#include <sys/utsname.h>
#endif      /* MAGIC_EDITING_TAG */

const char *SPACES = "                                                                               ";
int off = 0;
int on = 1;

int
ink_sys_name_release(char *name, int namelen, char *release, int releaselen)
{
  *name = 0;
  *release = 0;
#if (HOST_OS == freebsd) || (HOST_OS == darwin)
  int mib[2];
  size_t len = namelen;
  mib[0] = CTL_KERN;
  mib[1] = KERN_OSTYPE;

  if (sysctl(mib, 2, name, &len, NULL, 0) == -1)
    return -1;

  len = releaselen;
  mib[0] = CTL_KERN;
  mib[1] = KERN_OSRELEASE;

  if (sysctl(mib, 2, release, &len, NULL, 0) == -1)
    return -1;

  return 0;
#elif (HOST_OS == linux)
  struct utsname buf;
  int n;

  if (uname(&buf))
    return -1;

  n = strlen(buf.sysname);
  if (namelen <= n)
    n = namelen - 1;
  memcpy(name, buf.sysname, n);
  name[n] = 0;

  n = strlen(buf.release);
  if (releaselen <= n)
    n = releaselen - 1;
  memcpy(release, buf.release, n);
  release[n] = 0;

  return 0;
#else
  return -1;
#endif
}

int
ink_number_of_processors()
{
#if (HOST_OS == freebsd)
  int mib[2], n;
  mib[0] = CTL_HW;
  mib[1] = HW_NCPU;
  size_t len = sizeof(n);
  if (sysctl(mib, 2, &n, &len, NULL, 0) == -1)
    return 1;
  return n;
#else
  return sysconf(_SC_NPROCESSORS_ONLN); // number of Net threads
#endif
}
