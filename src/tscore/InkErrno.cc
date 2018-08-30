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
*/

#include "tscore/InkErrno.h"
#include "tscore/ink_assert.h"
#include <cstring>

const char *
InkStrerror(int ink_errno)
{
  if (ink_errno < INK_START_ERRNO) {
    return strerror(ink_errno);
  }

  switch (ink_errno) {
  case ENET_THROTTLING:
    return "ENET_THROTTLING";
  case ENET_CONNECT_TIMEOUT:
    return "ENET_CONNECT_TIMEOUT";
  case ENET_CONNECT_FAILED:
    return "ENET_CONNECT_FAILED";
  case ESOCK_DENIED:
    return "ESOCK_DENIED";
  case ESOCK_TIMEOUT:
    return "ESOCK_TIMEOUT";
  case ESOCK_NO_SOCK_SERVER_CONN:
    return "ESOCK_NO_SOCK_SERVER_CONN";
  case ECACHE_NO_DOC:
    return "ECACHE_NO_DOC";
  case ECACHE_DOC_BUSY:
    return "ECACHE_DOC_BUSY";
  case ECACHE_DIR_BAD:
    return "ECACHE_DIR_BAD";
  case ECACHE_BAD_META_DATA:
    return "ECACHE_BAD_META_DATA";
  case ECACHE_READ_FAIL:
    return "ECACHE_READ_FAIL";
  case ECACHE_WRITE_FAIL:
    return "ECACHE_WRITE_FAIL";
  case ECACHE_MAX_ALT_EXCEEDED:
    return "ECACHE_MAX_ALT_EXCEEDED";
  case ECACHE_NOT_READY:
    return "ECACHE_NOT_READY";
  case ECACHE_ALT_MISS:
    return "ECACHE_ALT_MISS";
  case ECACHE_BAD_READ_REQUEST:
    return "ECACHE_BAD_READ_REQUEST";
  case EHTTP_ERROR:
    return "EHTTP_ERROR";
  }

  if (ink_errno > HTTP_ERRNO) {
    return "EHTTP (unknown)";
  }

  if (ink_errno > CACHE_ERRNO) {
    return "ECACHE (unknown)";
  }

  if (ink_errno > NET_ERRNO) {
    return "ENET (unknown)";
  }

  // Verify there are no holes in the error ranges.
  ink_assert(INK_START_ERRNO == SOCK_ERRNO);
  ink_assert(ink_errno > SOCK_ERRNO);

  return "ESOCK (unknown)";
}
