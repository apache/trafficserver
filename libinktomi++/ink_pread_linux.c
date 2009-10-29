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
#include <unistd.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <string.h>
#include <memory.h>
#include "ink_pread.h"
#include "ink_assert.h"
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */


ssize_t
_read_from_middle_of_file(int fd, void *buf, size_t nbytes, off_t offset)
{
  int i;
  mlock(buf, nbytes);
  i = pread(fd, buf, nbytes, offset);
  munlock(buf, nbytes);
  return i;
}

ssize_t
_write_to_middle_of_file(int fd, void *buf, size_t nbytes, off_t offset)
{
  int i;
  mlock(buf, nbytes);
  i = pwrite(fd, buf, nbytes, offset);
  munlock(buf, nbytes);
  return i;
}
