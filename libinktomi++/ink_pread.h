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

#ifndef _ink_pread_h_
#define _ink_pread_h_

/*
 * Provide a consistent way to include pread functionality
 */

#if (HOST_OS == linux)

#ifdef __cplusplus
extern "C"
{
#endif

  ssize_t _read_from_middle_of_file(int fd, void *buf, size_t nbytes, off_t offset);
  ssize_t _write_to_middle_of_file(int fd, void *buf, size_t nbytes, off_t offset);

#ifdef __cplusplus
}
#endif

#else  // linux check

/*
 * On Solaris, etc, import the system definitions of pread/pwrite..
 */
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#endif  // linux check

#endif
