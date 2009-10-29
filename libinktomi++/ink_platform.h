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

#ifndef _ink_platform_h
#define _ink_platform_h

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

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

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <signal.h>

#if (HOST_OS != linux) && (HOST_OS != freebsd)
#include <siginfo.h>
#endif

#if (HOST_OS != freebsd)
#include <wait.h>
#endif

#include <syslog.h>
#include <pwd.h>
#include <strings.h>
#include <poll.h>

#if (HOST_OS == linux)
#include <sys/epoll.h>
#endif

#if (HOST_OS != freebsd)
#include <values.h>
#include <alloca.h>
#endif

#include <errno.h>
#include <dirent.h>

#if (HOST_OS != linux) && (HOST_OS != freebsd)
#include <cpio.h>
#endif

struct ifafilt;
#include <net/if.h>

#if (HOST_OS != linux) && (HOST_OS != freebsd)
#include <stropts.h>
#endif

#ifdef __alpha
//
// Gnu C++ doesn't define __STDC__ == 0 as needed to
// have ip_hl be defined.
//
#if defined(__GNUC__) && !defined(__STDC__)
#define __STDC__ 0
#endif
#  include <netinet/ip.h>
#  include <machine/endian.h>
#  include <sys/ioctl.h>
#elif (HOST_OS == linux)
#  include <netinet/ip.h>
#  include <endian.h>
#  include <sys/ioctl.h>
#  ifdef __KERNEL__
#   include <linux/sockios.h>
#  endif
#elif (HOST_OS == freebsd)
#  include <netinet/ip.h>
#  include <machine/endian.h>
#  include <sys/ioctl.h>
#else
#  include <netinet/ip.h>
#  include <sys/byteorder.h>
#  include <sys/sockio.h>
#endif

#include <netinet/ip_icmp.h>

#if (HOST_OS != linux) && (HOST_OS != freebsd)
#undef __P
#endif

#include <resolv.h>


#if (HOST_OS == linux)
typedef unsigned int in_addr_t;
#endif

#if (HOST_OS == linux)
#include <sys/sysinfo.h>
#elif (HOST_OS == freebsd)
#include <sys/sysctl.h>
#else
#include <sys/systeminfo.h>
#endif

#include <dlfcn.h>

#endif /* _PLATFORM_H_ */
