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

#pragma once

#include "tscore/ink_config.h"

// Gnu C++ doesn't define __STDC__ == 0 as needed to
// have ip_hl be defined.
#if defined(__GNUC__) && !defined(__STDC__)
#define __STDC__ 0
#endif

#include <fcntl.h>
#include <limits.h> // NOLINT(modernize-deprecated-headers)
#include <assert.h> // NOLINT(modernize-deprecated-headers)
#include <time.h>   // NOLINT(modernize-deprecated-headers)
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <errno.h> // NOLINT(modernize-deprecated-headers)
#include <syslog.h>
#include <pwd.h>
#include <poll.h>
#include <dirent.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include <sys/param.h>
#include <sys/un.h>

#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>

struct ifafilt;
#include <net/if.h>

#include <stdlib.h> // NOLINT(modernize-deprecated-headers)
#include <ctype.h>  // NOLINT(modernize-deprecated-headers)
#include <string.h> // NOLINT(modernize-deprecated-headers)

#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
// This is a bit of a hack, to get the more linux specific tcp_info struct ...
#if __has_include(<linux/tcp.h>)
#include <linux/tcp.h>
#else
#include <netinet/tcp.h>
#endif
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <netdb.h>

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <arpa/nameser_compat.h>

#include <signal.h> // NOLINT(modernize-deprecated-headers)

#if TS_USE_EPOLL
#include <sys/epoll.h>
#endif
#if TS_USE_KQUEUE
#include <sys/event.h>
#endif

#if __has_include(<alloca.h>)
#include <alloca.h>
#endif

#include "tscore/ink_endian.h"

#include <sys/ioctl.h>

#if defined(__linux__)
using in_addr_t = unsigned int;
#endif

#if __has_include(<sys/sysinfo.h>)
#include <sys/sysinfo.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
#if __has_include(<sys/sysctl.h>)
#include <sys/sysctl.h>
#endif
#endif

#include <dlfcn.h>

#include <float.h> // NOLINT(modernize-deprecated-headers)

#if __has_include(<sys/sysmacros.h>)
#include <sys/sysmacros.h>
#endif

#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#endif

// Unconditionally included headers that depend on conditionally included ones.
#include <resolv.h> // Must go after the netinet includes for FreeBSD

#ifndef PATH_NAME_MAX
#define PATH_NAME_MAX \
  4096 // instead of PATH_MAX which is inconsistent
       // on various OSs (linux-4096,osx/bsd-1024,
       //                 windows-260,etc)
#endif

// If kernel headers do not support IPPROTO_MPTCP definition
#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

#ifndef SOL_MPTCP
#define SOL_MPTCP 284
#endif

// Undefined in upstream until 5.16
#ifndef MPTCP_INFO
#define MPTCP_INFO 1
#endif
