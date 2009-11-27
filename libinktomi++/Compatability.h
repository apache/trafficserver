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

#include "ink_port.h"
#include "ink_resource.h"

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

// Define the directory separator for UNIX
#define DIR_SEP "/"

#define _O_ATTRIB_NORMAL  0x0000
#define _O_ATTRIB_OVERLAPPED 0x0000

//
// If you the gethostbyname() routines on your system are automatically
// re-entrent (as in Digital Unix), define the following
//
#if (HOST_OS == linux)
#define NEED_ALTZONE_DEFINED
#define MAP_SHARED_MAP_NORESERVE (MAP_SHARED)
#else
#define MAP_SHARED_MAP_NORESERVE (MAP_SHARED | MAP_NORESERVE)
#endif

#if (HOST_OS == linux)
/* typedef int rlim_t; */
typedef long paddr_t;
#ifndef __x86_64
typedef unsigned long long uint64_t;
#endif

extern "C"
{
  int ink__exit(int);
#define _exit(val) ink__exit(val)
}
#endif

#if (HOST_OS == freebsd) || (HOST_OS == darwin)
typedef long paddr_t;
typedef unsigned int in_addr_t;
#endif

#define NEED_HRTIME

#include <stdio.h>

#if (HOST_OS == freebsd)
static inline int
ink_sscan_longlong(const char *str, ink64 * value)
{
  return sscanf(str, "%qd", value);
}
#else
static inline int
ink_sscan_longlong(const char *str, ink64 * value)
{
  // coverity[secure_coding]
  return sscanf(str, "%lld", value);
}
#endif

#if (HOST_OS == linux)
#define BCOPY_TYPE (char *)
#else
#define BCOPY_TYPE (void *)
extern "C" void bcopy(const void *s1, void *s2, size_t n);
#endif

//
// Attempt (in a resonably portable manner) to determine
// if this O/S rev support pread/pwrite
//
#include <sys/syscall.h>

// Some ugliness around pread() vs SYS_pread64 syscall
#if defined (SYS_pread64)
#  define SYS_pread SYS_pread64
#endif
#if defined (SYS_pwrite64)
#  define SYS_pwrite SYS_pwrite64
#endif

#if !defined (SYS_pread)

#include <unistd.h>
#include <sys/file.h>

//
// The following functions also check that there are no races
// for the file descriptor. This only occurs in debug mode.
//

static inline ssize_t
read_from_middle_of_file(int fildes, void *buf, size_t nbyte, off_t offset)
{

  off_t there = lseek(fildes, offset, SEEK_SET);

  if (there == (off_t) - 1) {
    return -1;
  }
  ssize_t read_ret = read(fildes, buf, nbyte);
  if (read_ret == -1) {
    return -1;
  }
  return read_ret;
}

static inline ssize_t
write_to_middle_of_file(int fildes, void *buf, size_t nbyte, off_t offset)
{

  off_t there = lseek(fildes, offset, SEEK_SET);

  if (there == (off_t) - 1) {
    return -1;
  }
  ssize_t write_ret = write(fildes, buf, nbyte);
  if (write_ret == -1) {
    return -1;
  }
  return write_ret;
}

static inline ssize_t
pread(int fildes, void *buf, size_t nbyte, off_t offset)
{
  return read_from_middle_of_file(fildes, buf, nbyte, offset);
}
static inline ssize_t
pwrite(int fildes, void *buf, size_t nbytes, off_t offset)
{
  return write_to_middle_of_file(fildes, buf, nbytes, offset);
}

#elif (HOST_OS == linux)

#include "ink_pread.h"

static inline ssize_t
read_from_middle_of_file(int fildes, void *buf, size_t nbytes, off_t offset)
{
  return _read_from_middle_of_file(fildes, buf, nbytes, offset);
}

static inline ssize_t
write_to_middle_of_file(int fildes, void *buf, size_t nbytes, off_t offset)
{
  return _write_to_middle_of_file(fildes, buf, nbytes, offset);
}

static inline ssize_t
__ink_pread(int fildes, void *buf, size_t nbyte, off_t offset)
{
  return _read_from_middle_of_file(fildes, buf, nbyte, offset);
}
static inline ssize_t
__ink_pwrite(int fildes, void *buf, size_t nbytes, off_t offset)
{
  return _write_to_middle_of_file(fildes, buf, nbytes, offset);
}

#define pread   __ink_pread
#define pwrite  __ink_pwrite

#else

#include "ink_pread.h"

static inline ssize_t
read_from_middle_of_file(int fildes, void *buf, size_t nbytes, off_t offset)
{
  return pread(fildes, buf, nbytes, offset);
}

static inline ssize_t
write_to_middle_of_file(int fildes, void *buf, size_t nbytes, off_t offset)
{
  return pwrite(fildes, buf, nbytes, offset);
}

#endif

#define ink_open       open
#define ink_close      close
#define ink_lseek      lseek
#define ink_write      write
#define ink_pwrite     pwrite
#define ink_read       read
#define ink_pread      pread
#define ink_writev     writev
#define ink_readv      readv
#define ink_fsync      fsync
#define ink_ftruncate  ftruncate

#if (HOST_OS == freebsd)
#define ink_ftruncate64(_fd,_s)  ftruncate(_fd, (off_t)(_s))
#else
#define ink_ftruncate64  ftruncate64
#endif

#define ink_fstat      fstat
#define ink_mmap       mmap
#define ink_sleep      sleep

#include "Resource.h"

#endif
