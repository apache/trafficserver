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

#include <stdio.h>
#include <sys/types.h>

typedef char ink8;
typedef unsigned char inku8;
typedef short ink16;
typedef unsigned short inku16;
typedef int ink32;
typedef unsigned int inku32;
typedef long long ink64;
typedef unsigned long long inku64;
typedef off_t ink_off_t;

/*******************************************************************
 ** x86
  ******************************************************************/

#if (defined(i386) || defined(__i386))
# define ink_atomic_cas_long(x,y) ink_atomic_cas ((ink32*) x, y)
# define ink_atomic_swap_long(x,y) ink_atomic_swap ((ink32*) x, y)
#else
# define ink_atomic_cas_long(x,y) ink_atomic_cas64 ((ink64*) x, y)
# define ink_atomic_swap_long(x,y) ink_atomic_swap64 ((ink64*) x, y)
#endif

#define _CRTIMP
#define HAVE_64_BIT

#if (HOST_OS == linux) || (HOST_OS == freebsd)
#define POSIX_THREAD
#define POSIX_THREAD_10031c
#else
/* # define POSIX_THREAD */
#error "Unknown OS!"
#endif


#if (HOST_OS == freebsd)
#define ETIME ETIMEDOUT
#define ENOTSUP EOPNOTSUPP
#define NO_MEMALIGN
#define MAXINT INT_MAX
#endif

#define NUL '\0'

// copy from ink_ntio.h
typedef enum
{
  keSocket = 0xbad,
  keFile,
  KeDontCare
} teFDType;

/********************************* PROTOTYPES *******************************/

void ink_port_check_type_sizes();

#endif
