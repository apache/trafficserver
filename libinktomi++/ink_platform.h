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

#ifdef HAVE_SIGINFO_H
#include <siginfo.h>
#endif

#ifdef HAVE_WAIT_H
#include <wait.h>
#endif

#include <syslog.h>
#include <pwd.h>
#include <strings.h>
#include <poll.h>

#if defined(USE_EPOLL)
#include <sys/epoll.h>
#elif defined(USE_KQUEUE)
#include <sys/event.h>
#elif defined(USE_PORT)
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

#ifdef HAVE_NETINET_IN_H
  #include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_IP_H
  #include <netinet/ip.h>
#endif

#ifdef HAVE_NETINET_IN_SYSTM_H
  #include <netinet/in_systm.h>
#endif

#ifdef HAVE_NETINET_IP_ICMP_H
  #include <netinet/ip_icmp.h>
#endif

#ifdef HAVE_MACHINE_ENDIAN_H
#  include <machine/endian.h>
#endif

#ifdef HAVE_ENDIAN_H
#  include <endian.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif


#ifdef HAVE_SYS_BYTEORDER_H
#  include <sys/byteorder.h>
#endif

#ifdef HAVE_SYS_SOCKIO_H
#  include <sys/sockio.h>
#endif

#include <resolv.h>


#if (HOST_OS == linux)
typedef unsigned int in_addr_t;
#endif

#ifdef HAVE_SYS_SYSINFO_H
#  include <sys/sysinfo.h>
#endif

#if (HOST_OS != darwin)
#ifdef HAVE_SYS_SYSCTL_H
#  include <sys/sysctl.h>
#endif
#endif

#ifdef HAVE_SYS_SYSTEMINFO_H
#  include <sys/systeminfo.h>
#endif

#include <dlfcn.h>

#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif
#ifdef HAVE_ARPA_NAMESER_H
#  include <arpa/nameser.h>
#endif
#ifdef HAVE_ARPA_NAMESER_COMPAT_H
#  include <arpa/nameser_compat.h>
#endif

#ifdef HAVE_MATH_H
#include <math.h>
#endif

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#ifndef PATH_NAME_MAX
#define PATH_NAME_MAX 511 // instead of PATH_MAX which is inconsistent
                          // on various OSs (linux-4096,osx/bsd-1024,
                          //                 windows-260,etc)
#endif

#endif /* _PLATFORM_H_ */
