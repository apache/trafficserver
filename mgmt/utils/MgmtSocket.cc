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

#include "tscore/ink_platform.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_cap.h"
#include "MgmtSocket.h"

#if HAVE_UCRED_H
#include <ucred.h>
#endif

//-------------------------------------------------------------------------
// defines
//-------------------------------------------------------------------------

#define MGMT_MAX_TRANSIENT_ERRORS 64

//-------------------------------------------------------------------------
// transient_error
//-------------------------------------------------------------------------

bool
mgmt_transient_error()
{
  switch (errno) {
  case EINTR:
  case EAGAIN:

#ifdef ENOMEM
  case ENOMEM:
#endif

#ifdef ENOBUF
  case ENOBUF:
#endif

#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
  case EWOULDBLOCK:
#endif

    return true;

  default:
    return false;
  }
}

//-------------------------------------------------------------------------
// system calls (based on implementation from UnixSocketManager)
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// mgmt_accept
//-------------------------------------------------------------------------

int
mgmt_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
  int r, retries;
  ink_assert(*addrlen != 0);
  for (retries = 0; retries < MGMT_MAX_TRANSIENT_ERRORS; retries++) {
    r = ::accept(s, addr, (socklen_t *)addrlen);
    if (r >= 0) {
      return r;
    }
    if (!mgmt_transient_error()) {
      break;
    }
  }
  return r;
}

//-------------------------------------------------------------------------
// mgmt_fopen
//-------------------------------------------------------------------------

FILE *
mgmt_fopen(const char *filename, const char *mode)
{
  FILE *f;
  int retries;
  for (retries = 0; retries < MGMT_MAX_TRANSIENT_ERRORS; retries++) {
    // no leak here as f will be returned if it is > 0
    // coverity[overwrite_var]
    f = ::fopen(filename, mode);
    if (f != nullptr) {
      return f;
    }
    if (!mgmt_transient_error()) {
      break;
    }
  }
  return f;
}

//-------------------------------------------------------------------------
// mgmt_open
//-------------------------------------------------------------------------

int
mgmt_open(const char *path, int oflag)
{
  int r, retries;
  for (retries = 0; retries < MGMT_MAX_TRANSIENT_ERRORS; retries++) {
    r = ::open(path, oflag);
    if (r >= 0) {
      return r;
    }
    if (!mgmt_transient_error()) {
      break;
    }
  }
  return r;
}

//-------------------------------------------------------------------------
// mgmt_open_mode
//-------------------------------------------------------------------------

int
mgmt_open_mode(const char *path, int oflag, mode_t mode)
{
  int r, retries;
  for (retries = 0; retries < MGMT_MAX_TRANSIENT_ERRORS; retries++) {
    r = ::open(path, oflag, mode);
    if (r >= 0) {
      return r;
    }
    if (!mgmt_transient_error()) {
      break;
    }
  }
  return r;
}

//-------------------------------------------------------------------------
// mgmt_open_mode_elevate
//-------------------------------------------------------------------------

int
mgmt_open_mode_elevate(const char *path, int oflag, mode_t mode, bool elevate_p)
{
  int r, retries;
  for (retries = 0; retries < MGMT_MAX_TRANSIENT_ERRORS; retries++) {
    r = elevate_p ? elevating_open(path, oflag, mode) : ::open(path, oflag, mode);
    if (r >= 0) {
      return r;
    }
    if (!mgmt_transient_error()) {
      break;
    }
  }
  return r;
}
//-------------------------------------------------------------------------
// mgmt_select
//-------------------------------------------------------------------------

int
mgmt_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout)
{
// Note: Linux select() has slight different semantics.  From the
// man page: "On Linux, timeout is modified to reflect the amount of
// time not slept; most other implementations do not do this."
// Linux select() can also return ENOMEM, so we espeically need to
// protect the call with the transient error retry loop.
// Fortunately, because of the Linux timeout handling, our
// mgmt_select call will still timeout correctly, rather than
// possibly extending our timeout period by up to
// MGMT_MAX_TRANSIENT_ERRORS times.
#if defined(linux)
  int r, retries;
  for (retries = 0; retries < MGMT_MAX_TRANSIENT_ERRORS; retries++) {
    r = ::select(nfds, readfds, writefds, errorfds, timeout);
    if (r >= 0) {
      return r;
    }
    if (!mgmt_transient_error()) {
      break;
    }
  }
  return r;
#else
  return ::select(nfds, readfds, writefds, errorfds, timeout);
#endif
}

//-------------------------------------------------------------------------
// mgmt_sendto
//-------------------------------------------------------------------------

int
mgmt_sendto(int fd, void *buf, int len, int flags, struct sockaddr *to, int tolen)
{
  int r, retries;
  for (retries = 0; retries < MGMT_MAX_TRANSIENT_ERRORS; retries++) {
    r = ::sendto(fd, (char *)buf, len, flags, to, tolen);
    if (r >= 0) {
      return r;
    }
    if (!mgmt_transient_error()) {
      break;
    }
  }
  return r;
}

//-------------------------------------------------------------------------
// mgmt_socket
//-------------------------------------------------------------------------

int
mgmt_socket(int domain, int type, int protocol)
{
  int r, retries;
  for (retries = 0; retries < MGMT_MAX_TRANSIENT_ERRORS; retries++) {
    r = ::socket(domain, type, protocol);
    if (r >= 0) {
      return r;
    }
    if (!mgmt_transient_error()) {
      break;
    }
  }
  return r;
}

/***************************************************************************
 * mgmt_write_timeout
 *
 * purpose: checks if the specified socket is ready to be written too; only
 *          checks for the specified time
 * input: fd   - the socket to wait for
 *        sec  - time to wait in secs
 *        usec - time to wait in usecs
 * output: return   0 if time expires and the fd is not ready to be written
 *         return > 0 (actually 1) if fd is ready to be written
 *         return < 0 if error
 ***************************************************************************/
int
mgmt_write_timeout(int fd, int sec, int usec)
{
  struct timeval timeout;
  fd_set writeSet;
  timeout.tv_sec  = sec;
  timeout.tv_usec = usec;

  if (fd < 0 || fd >= (int)FD_SETSIZE) {
    errno = EBADF;
    return -1;
  }

  FD_ZERO(&writeSet);
  FD_SET(fd, &writeSet);

  if (sec < 0 && usec < 0) {
    // blocking select; only returns when fd is ready to write
    return (mgmt_select(fd + 1, nullptr, &writeSet, nullptr, nullptr));
  } else {
    return (mgmt_select(fd + 1, nullptr, &writeSet, nullptr, &timeout));
  }
}

/***************************************************************************
 * mgmt_read_timeout
 *
 * purpose: need timeout for socket after sending a request and waiting to
 *          read reply check to see if anything to read;
 *          but only wait for fixed time specified in timeout struct
 * input: fd   - the socket to wait for
 *        sec  - time to wait in secs
 *        usec - time to wait in usecs
 * output: returns 0 if time expires and the fd is not ready
 *         return > 0 (actually 1) if fd is ready to read
 * reason: the client could send a reply, but if TM is down or has
 *         problems sending a reply then the client could end up hanging,
 *         waiting to read a replay from the local side
 ***************************************************************************/
int
mgmt_read_timeout(int fd, int sec, int usec)
{
  struct timeval timeout;
  fd_set readSet;
  timeout.tv_sec  = sec;
  timeout.tv_usec = usec;

  if (fd < 0 || fd >= (int)FD_SETSIZE) {
    errno = EBADF;
    return -1;
  }

  FD_ZERO(&readSet);
  FD_SET(fd, &readSet);

  return mgmt_select(fd + 1, &readSet, nullptr, nullptr, &timeout);
}

bool
mgmt_has_peereid()
{
#if HAVE_GETPEEREID
  return true;
#elif HAVE_GETPEERUCRED
  return true;
#elif TS_HAS_SO_PEERCRED
  return true;
#else
  return false;
#endif
}

int
mgmt_get_peereid(int fd, uid_t *euid, gid_t *egid)
{
  *euid = -1;
  *egid = -1;

#if HAVE_GETPEEREID
  return getpeereid(fd, euid, egid);
#elif HAVE_GETPEERUCRED
  ucred_t *ucred;

  if (getpeerucred(fd, &ucred) == -1) {
    return -1;
  }

  *euid = ucred_geteuid(ucred);
  *egid = ucred_getegid(ucred);
  ucred_free(ucred);
  return 0;
#elif TS_HAS_SO_PEERCRED
  struct ucred cred;
  socklen_t credsz = sizeof(cred);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &credsz) == -1) {
    return -1;
  }

  *euid = cred.uid;
  *egid = cred.gid;
  return 0;
#else
  (void)fd;
  errno = ENOTSUP;
  return -1;
#endif
}
