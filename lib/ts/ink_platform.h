/** @file

  Platform specific defines and includes, this is to make the build
  more portable.

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

#ifndef _ink_platform_h
#define _ink_platform_h

#include "ts/ink_config.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <ctype.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <errno.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <fcntl.h>

#include <limits.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/resource.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include <sys/param.h>
#include <sys/un.h>

#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mman.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif
#ifdef HAVE_NETINET_IP_ICMP_H
#include <netinet/ip_icmp.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif

#include <signal.h>
#ifdef HAVE_SIGINFO_H
#include <siginfo.h>
#endif
#ifdef HAVE_WAIT_H
#include <wait.h>
#endif

#include <syslog.h>
#include <pwd.h>
#include <poll.h>

#if TS_USE_EPOLL
#include <sys/epoll.h>
#endif
#if TS_USE_KQUEUE
#include <sys/event.h>
#endif
#if TS_USE_PORT
#include <port.h>
#endif

#ifdef HAVE_VALUES_H
#include <values.h>
#endif
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <errno.h>
#include <dirent.h>

#ifdef HAVE_CPIO_H
#include <cpio.h>
#endif

struct ifafilt;
#include <net/if.h>

#ifdef HAVE_STROPTS_H
#include <stropts.h>
#endif

//
// Gnu C++ doesn't define __STDC__ == 0 as needed to
// have ip_hl be defined.
//
#if defined(__GNUC__) && !defined(__STDC__)
#define __STDC__ 0
#endif

#ifdef HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#endif
#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif
#ifdef HAVE_SYS_BYTEORDER_H
#include <sys/byteorder.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#include <resolv.h>

#if defined(linux)
typedef unsigned int in_addr_t;
#endif

#ifdef HAVE_SYS_SYSINFO_H
#include <sys/sysinfo.h>
#endif

#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#ifdef HAVE_SYS_SYSTEMINFO_H
#include <sys/systeminfo.h>
#endif

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#ifdef HAVE_MATH_H
#include <math.h>
#endif
#ifdef HAVE_FLOAT_H
#include <float.h>
#endif

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#ifndef PATH_NAME_MAX
#define PATH_NAME_MAX 4096 // instead of PATH_MAX which is inconsistent
                           // on various OSs (linux-4096,osx/bsd-1024,
                           //                 windows-260,etc)
#endif

#endif /* _ink_platform_h */
