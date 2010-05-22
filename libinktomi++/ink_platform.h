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

#include "ink_config.h"

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

#if ATS_HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if ATS_HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if ATS_HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif
#ifdef ATS_HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
#if ATS_HAVE_NETINET_IP_ICMP_H
# include <netinet/ip_icmp.h>
#endif
#include <netdb.h>
#ifdef ATS_HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#ifdef ATS_HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif
#ifdef ATS_HAVE_ARPA_NAMESER_COMPAT_H
# include <arpa/nameser_compat.h>
#endif

#include <signal.h>
#if ATS_HAVE_SIGINFO_H
# include <siginfo.h>
#endif
#if ATS_HAVE_WAIT_H
# include <wait.h>
#endif

#include <syslog.h>
#include <pwd.h>
#include <strings.h>
#include <poll.h>

#if ATS_USE_EPOLL
#include <sys/epoll.h>
#endif
#if ATS_USE_KQUEUE
#include <sys/event.h>
#endif
#if ATS_USE_PORT
#include <port.h>
#endif


#if ATS_HAVE_VALUES_H
# include <values.h>
#endif
#if ATS_HAVE_ALLOCA_H
# include <alloca.h>
#endif

#include <errno.h>
#include <dirent.h>

#if ATS_HAVE_CPIO_H
# include <cpio.h>
#endif

struct ifafilt;
#include <net/if.h>

#if ATS_HAVE_STROPTS_H
#include <stropts.h>
#endif

//
// Gnu C++ doesn't define __STDC__ == 0 as needed to
// have ip_hl be defined.
//
#if defined(__GNUC__) && !defined(__STDC__)
#define __STDC__ 0
#endif

#if ATS_HAVE_MACHINE_ENDIAN_H
# include <machine/endian.h>
#endif
#if ATS_HAVE_ENDIAN_H
# include <endian.h>
#endif
#if ATS_HAVE_SYS_BYTEORDER_H
# include <sys/byteorder.h>
#endif

#if ATS_HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#if ATS_HAVE_SYS_SOCKIO_H
# include <sys/sockio.h>
#endif

#include <resolv.h>


#if defined(linux)
typedef unsigned int in_addr_t;
#endif

#if ATS_HAVE_SYS_SYSINFO_H
# include <sys/sysinfo.h>
#endif

#if !defined(darwin)
#ifdef HAVE_SYS_SYSCTL_H
#  include <sys/sysctl.h>
# endif
#endif
#ifdef HAVE_SYS_SYSTEMINFO_H
#  include <sys/systeminfo.h>
#endif

#include <dlfcn.h>

#if ATS_HAVE_MATH_H
# include <math.h>
#endif

#if ATS_HAVE_SYS_SYSMACROS_H
# include <sys/sysmacros.h>
#endif

#ifndef PATH_NAME_MAX
#define PATH_NAME_MAX 511 // instead of PATH_MAX which is inconsistent
                          // on various OSs (linux-4096,osx/bsd-1024,
                          //                 windows-260,etc)
#endif

#endif /* _PLATFORM_H_ */
