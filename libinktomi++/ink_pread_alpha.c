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

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/file.h>
#ifdef __alpha
#include <syscall.h>
#endif
#include "ink_assert.h"
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */

/*
 * This file is for systems (like Digital Unix) that don't provide
 * a pread system call
 */

#pragma inline(lock_fd)
static void
lock_fd(int fildes)
{
  int flock_ret = flock(fildes, LOCK_EX);
  ink_assert(flock_ret >= 0);
}

#pragma inline(unlock_fd)
static void
unlock_fd(int fildes)
{
  int flock_ret = flock(fildes, LOCK_UN);
}

#if defined(__alpha) && !defined(SYS_pread)
ssize_t
pread(int fildes, void *buf, size_t nbyte, off_t offset)
{
  lock_fd(fildes);
  {

    off_t here = lseek(fildes, 0, SEEK_CUR);
    off_t there = lseek(fildes, offset, SEEK_SET);
    ssize_t read_ret;

    if (there == (off_t) - 1) {
      unlock_fd(fildes);
      return -1;
    }
    read_ret = read(fildes, buf, nbyte);
    if (read_ret == -1) {
      unlock_fd(fildes);
      return -1;
    }
    lseek(fildes, SEEK_SET, here);

    unlock_fd(fildes);
    return read_ret;
  }
}

ssize_t
pwrite(int fildes, void *buf, size_t nbyte, off_t offset)
{

  lock_fd(fildes);
  {

    off_t here = lseek(fildes, 0, SEEK_CUR);
    off_t there = lseek(fildes, offset, SEEK_SET);
    ssize_t write_ret;

    if (there == (off_t) - 1) {
      unlock_fd(fildes);
      return -1;
    }
    write_ret = write(fildes, buf, nbyte);
    if (write_ret == -1) {
      unlock_fd(fildes);
      return -1;
    }
    lseek(fildes, SEEK_SET, here);

    unlock_fd(fildes);
    return write_ret;
  }
}
#endif
