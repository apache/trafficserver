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

/***************************************************************************
  Socket operations



***************************************************************************/
#pragma once

#include <cstdint>
#include "tscore/ink_platform.h"
#include "tscore/ink_apidefs.h"

int safe_bind(int s, struct sockaddr const *name, int namelen);
int safe_listen(int s, int backlog);
int safe_getsockname(int s, struct sockaddr *name, int *namelen);
int safe_getpeername(int s, struct sockaddr *name, int *namelen);

int safe_setsockopt(int s, int level, int optname, const void *optval, int optlen);
int safe_getsockopt(int s, int level, int optname, void *optval, int *optlen);

static inline int
setsockopt_on(int s, int level, int optname)
{
  int on = 1;
  return safe_setsockopt(s, level, optname, &on, sizeof(on));
}

static inline int
setsockopt_off(int s, int level, int optname)
{
  int on = 0;
  return safe_setsockopt(s, level, optname, &on, sizeof(on));
}

/** Repeat write calls to fd until all len bytes are written.
 *
 * @param[in] fd The file descriptor to write to.
 * @param[in] buffer The buffer of bytes to write into fd.
 * @param[in] len The number of bytes in buffer to write into fd.
 *
 * @return The number of bytes written or -1 if there was an error writing to
 * the file descriptor. If -1 is returned, errno should indicate why the
 * write failed.
 */
std::int64_t safe_write(int fd, const char *buffer, int len);

int safe_fcntl(int fd, int cmd, int arg);
int safe_ioctl(int fd, int request, char *arg);

int safe_set_fl(int fd, int arg);
int safe_clr_fl(int fd, int arg);

int safe_blocking(int fd);
int safe_nonblocking(int fd);

/** Poll on fd until it is ready for reading.
 *
 * @param[in] fd The file descriptor to poll on.
 * @param[in] timeout_msec How many milliseconds to poll on the socket before
 *   giving up.
 *
 * @return 1 if the socket is ready for reading, -1 if there was an error, and
 * 0 for timeout.
 */
int read_ready(int fd, int timeout_msec = 0);

/** Poll on fd until it is ready for writing.
 *
 * @param[in] fd The file descriptor to poll on.
 * @param[in] timeout_msec How many milliseconds to poll on the socket before
 *   giving up.
 *
 * @return 1 if the socket is ready for writing, -1 if there was an error, and
 * 0 for timeout.
 */
int write_ready(int fd, int timeout_msec = 0);

char fd_read_char(int fd);
int  fd_read_line(int fd, char *s, int len);

int close_socket(int s);
int write_socket(int s, const char *buffer, int length);
int read_socket(int s, char *buffer, int length);

int bind_unix_domain_socket(const char *path, mode_t mode);
