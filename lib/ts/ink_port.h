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

  Ink_port.h

  Definitions & declarations to faciliate inter-architecture portability.

 ****************************************************************************/

#if !defined (_ink_port_h_)
#define	_ink_port_h_

#include "ink_config.h"
#include <stddef.h>

#ifdef HAVE_STDINT_H
#define __STDC_LIMIT_MACROS
# include <stdint.h>
#else
// TODO: Add "standard" int types?
#endif

#ifdef HAVE_INTTYPES_H
# define __STDC_FORMAT_MACROS 1
# include <inttypes.h>
#else
// TODO: add PRI*64 stuff?
#endif

#ifndef INT64_MIN
#define INT64_MAX (9223372036854775807LL)
#define INT64_MIN (-INT64_MAX -1LL)
#define INTU32_MAX (4294967295U)
#define INT32_MAX (2147483647)
#define INT32_MIN (-2147483647-1)
#endif
// Hack for MacOSX, have to take this out of the group above.
#ifndef INTU64_MAX
#define INTU64_MAX (18446744073709551615ULL)
#endif

#define POSIX_THREAD
#define POSIX_THREAD_10031c

#ifndef ETIME
#ifdef ETIMEDOUT
#define ETIME ETIMEDOUT
#endif
#endif

#ifndef ENOTSUP
#ifdef EOPNOTSUPP
#define ENOTSUP EOPNOTSUPP
#endif
#endif

#if defined(darwin)
#define RENTRENT_GETHOSTBYNAME
#define RENTRENT_GETHOSTBYADDR
#endif

#define NUL '\0'

// Need to use this to avoid problems when calling variadic functions
// with many arguments. In such cases, a raw '0' or NULL can be
// interpreted as 32 bits
#define NULL_PTR static_cast<void*>(0)

#endif
