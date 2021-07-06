/** @file

  Error code defines

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

  @section details Details

  Contains all the errno codes that we may need (above and beyond those
  provided by /usr/include/errno.h)

*/

#pragma once
#include <cerrno>

#define INK_START_ERRNO 20000

#define SOCK_ERRNO INK_START_ERRNO
#define NET_ERRNO INK_START_ERRNO + 100
#define CACHE_ERRNO INK_START_ERRNO + 400
#define HTTP_ERRNO INK_START_ERRNO + 600

#define ENET_THROTTLING (NET_ERRNO + 1)
#define ENET_CONNECT_TIMEOUT (NET_ERRNO + 2)
#define ENET_CONNECT_FAILED (NET_ERRNO + 3)
#define ENET_SSL_CONNECT_FAILED (NET_ERRNO + 4)
#define ENET_SSL_FAILED (NET_ERRNO + 5)

#define ESOCK_DENIED (SOCK_ERRNO + 0)
#define ESOCK_TIMEOUT (SOCK_ERRNO + 1)
#define ESOCK_NO_SOCK_SERVER_CONN (SOCK_ERRNO + 2)

#define ECACHE_NO_DOC (CACHE_ERRNO + 0)
#define ECACHE_DOC_BUSY (CACHE_ERRNO + 1)
#define ECACHE_DIR_BAD (CACHE_ERRNO + 2)
#define ECACHE_BAD_META_DATA (CACHE_ERRNO + 3)
#define ECACHE_READ_FAIL (CACHE_ERRNO + 4)
#define ECACHE_WRITE_FAIL (CACHE_ERRNO + 5)
#define ECACHE_MAX_ALT_EXCEEDED (CACHE_ERRNO + 6)
#define ECACHE_NOT_READY (CACHE_ERRNO + 7)
#define ECACHE_ALT_MISS (CACHE_ERRNO + 8)
#define ECACHE_BAD_READ_REQUEST (CACHE_ERRNO + 9)

#define EHTTP_ERROR (HTTP_ERRNO + 0)

const char *InkStrerror(int ink_errno);
