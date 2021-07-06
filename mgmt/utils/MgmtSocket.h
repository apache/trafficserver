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

#pragma once

#include "tscore/ink_platform.h"

//-------------------------------------------------------------------------
// transient_error
//-------------------------------------------------------------------------

bool mgmt_transient_error();

//-------------------------------------------------------------------------
// system calls (based on implementation from UnixSocketManager);
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// mgmt_accept
//-------------------------------------------------------------------------

int mgmt_accept(int s, struct sockaddr *addr, socklen_t *addrlen);

//-------------------------------------------------------------------------
// mgmt_fopen
//-------------------------------------------------------------------------

FILE *mgmt_fopen(const char *filename, const char *mode);

//-------------------------------------------------------------------------
// mgmt_open
//-------------------------------------------------------------------------

int mgmt_open(const char *path, int oflag);

//-------------------------------------------------------------------------
// mgmt_open_mode
//-------------------------------------------------------------------------

int mgmt_open_mode(const char *path, int oflag, mode_t mode);

//-------------------------------------------------------------------------
// mgmt_open_mode_elevate
//-------------------------------------------------------------------------

int mgmt_open_mode_elevate(const char *path, int oflag, mode_t mode, bool elevate_p = false);

//-------------------------------------------------------------------------
// mgmt_select
//-------------------------------------------------------------------------

int mgmt_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout);

//-------------------------------------------------------------------------
// mgmt_sendto
//-------------------------------------------------------------------------

int mgmt_sendto(int fd, void *buf, int len, int flags, struct sockaddr *to, int tolen);

//-------------------------------------------------------------------------
// mgmt_socket
//-------------------------------------------------------------------------

int mgmt_socket(int domain, int type, int protocol);

//-------------------------------------------------------------------------
// mgmt_write_timeout
//-------------------------------------------------------------------------
int mgmt_write_timeout(int fd, int sec, int usec);

//-------------------------------------------------------------------------
// mgmt_read_timeout
//-------------------------------------------------------------------------
int mgmt_read_timeout(int fd, int sec, int usec);

// Do we support passing Unix domain credentials on this platform?
bool mgmt_has_peereid();

// Get the Unix domain peer credentials.
int mgmt_get_peereid(int fd, uid_t *euid, gid_t *egid);
