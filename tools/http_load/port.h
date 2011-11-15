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

#if defined(__FreeBSD__)
#define OS_FreeBSD
#define ARCH "FreeBSD"
#elif defined(__OpenBSD__)
#define OS_OpenBSD
#define ARCH "OpenBSD"
#elif defined(__NetBSD__)
#define OS_NetBSD
#define ARCH "NetBSD"
#elif defined(linux)
#define OS_Linux
#define ARCH "Linux"
#elif defined(sun)
#define OS_Solaris
#define ARCH "Solaris"
#elif defined(__osf__)
#define OS_DigitalUnix
#define ARCH "DigitalUnix"
#elif defined(__svr4__)
#define OS_SysV
#define ARCH "SysV"
#else
#define OS_UNKNOWN
#define ARCH "UNKNOWN"
#endif

#ifdef OS_FreeBSD
#include <osreldate.h>
#define HAVE_DAEMON
#define HAVE_SETSID
#define HAVE_SETLOGIN
#define HAVE_WAITPID
#define HAVE_HSTRERROR
#define HAVE_TM_GMTOFF
#define HAVE_SENDFILE
#define HAVE_SCANDIR
#define HAVE_INT64T
#define HAVE_SRANDOMDEV
#ifdef SO_ACCEPTFILTER
#define HAVE_ACCEPT_FILTERS
#if ( __FreeBSD_version >= 411000 )
#define ACCEPT_FILTER_NAME "httpready"
#else
#define ACCEPT_FILTER_NAME "dataready"
#endif
#endif /* SO_ACCEPTFILTER */
#endif /* OS_FreeBSD */

#ifdef OS_OpenBSD
#define HAVE_DAEMON
#define HAVE_SETSID
#define HAVE_SETLOGIN
#define HAVE_WAITPID
#define HAVE_HSTRERROR
#define HAVE_TM_GMTOFF
#define HAVE_SCANDIR
#define HAVE_INT64T
#endif /* OS_OpenBSD */

#ifdef OS_NetBSD
#define HAVE_DAEMON
#define HAVE_SETSID
#define HAVE_SETLOGIN
#define HAVE_WAITPID
#define HAVE_HSTRERROR
#define HAVE_TM_GMTOFF
#define HAVE_SCANDIR
#define HAVE_INT64T
#endif /* OS_NetBSD */

#ifdef OS_Linux
#define HAVE_DAEMON
#define HAVE_SETSID
#define HAVE_WAITPID
#define HAVE_TM_GMTOFF
#define HAVE_SENDFILE
#define HAVE_LINUX_SENDFILE
#define HAVE_SCANDIR
#define HAVE_INT64T
#endif /* OS_Linux */

#ifdef OS_Solaris
#define HAVE_SETSID
#define HAVE_WAITPID
#define HAVE_MEMORY_H
#define HAVE_SIGSET
#define HAVE_INT64T
#endif /* OS_Solaris */

#ifdef OS_DigitalUnix
#define HAVE_SETSID
#define HAVE_SETLOGIN
#define HAVE_WAITPID
#define HAVE_SCANDIR
#define HAVE_TM_GMTOFF
#define NO_SNPRINTF
                                /* # define HAVE_INT64T *//* Digital Unix 4.0d doesn't have int64_t */
#endif /* OS_DigitalUnix */

#ifdef OS_SysV
#define HAVE_SETSID
#define HAVE_WAITPID
#define HAVE_MEMORY_H
#define HAVE_SIGSET
#endif /* OS_Solaris */
