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

#if !defined (_Compatability_h_)
#define _Compatability_h_

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>

#include "ink_defs.h"

// We can't use #define for min and max becuase it will conflict with
// other declarations of min and max functions.  This conflict
// occurs with STL
template<class T> T min(const T a, const T b)
{
  return a < b ? a : b;
}

template<class T> T max(const T a, const T b)
{
  return a > b ? a : b;
}

#define _O_ATTRIB_NORMAL  0x0000
#define _O_ATTRIB_OVERLAPPED 0x0000

//
// If you the gethostbyname() routines on your system are automatically
// re-entrent (as in Digital Unix), define the following
//
#if defined(linux)
#define NEED_ALTZONE_DEFINED
#define MAP_SHARED_MAP_NORESERVE (MAP_SHARED)
#elif defined(darwin)
#define MAP_SHARED_MAP_NORESERVE (MAP_SHARED)
#elif defined(solaris)
#define NEED_ALTZONE_DEFINED
#define MAP_SHARED_MAP_NORESERVE (MAP_SHARED | MAP_NORESERVE)
#else
#define MAP_SHARED_MAP_NORESERVE (MAP_SHARED | MAP_NORESERVE)
#endif

#if defined(darwin)
typedef uint32_t in_addr_t;
#endif

#define NEED_HRTIME

#endif
