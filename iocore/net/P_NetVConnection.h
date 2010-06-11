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

#include "I_NetVConnection.h"

TS_INLINE const struct sockaddr_storage &
NetVConnection::get_remote_addr()
{
  if (!got_remote_addr) {
    set_remote_addr();
    got_remote_addr = 1;
  }
  return remote_addr;
}

TS_INLINE unsigned int
NetVConnection::get_remote_ip()
{
  switch (get_remote_addr().ss_family) {
  case AF_INET:
    return (unsigned int)((struct sockaddr_in *)&(get_remote_addr()))->sin_addr.s_addr;
  default:
    return 0;
  }
}


TS_INLINE int
NetVConnection::get_remote_port()
{
  switch (get_remote_addr().ss_family) {
  case AF_INET:
    return ntohs(((struct sockaddr_in *)&(get_remote_addr()))->sin_port);
  case AF_INET6:
    return ntohs(((struct sockaddr_in6 *)&(get_remote_addr()))->sin6_port);
  default:
    return 0;
  }
}

TS_INLINE const struct sockaddr_storage &
NetVConnection::get_local_addr()
{
  if (!got_local_addr) {
    set_local_addr();
    switch (local_addr.ss_family) {
    case AF_INET:
      if (((struct sockaddr_in *)&(local_addr))->sin_addr.s_addr || ((struct sockaddr_in *)&(local_addr))->sin_port) {
        got_local_addr = 1;
      }
      break;
    case AF_INET6:
      if (((struct sockaddr_in6 *)&(local_addr))->sin6_addr.s6_addr || ((struct sockaddr_in6 *)&(local_addr))->sin6_port) {
        got_local_addr = 1;
      }
    }
  }
  return local_addr;
}

TS_INLINE unsigned int
NetVConnection::get_local_ip()
{
  switch (get_local_addr().ss_family) {
  case AF_INET:
    return (unsigned int)((struct sockaddr_in *)&(get_local_addr()))->sin_addr.s_addr;
  default:
    return 0;
  }
}

TS_INLINE int
NetVConnection::get_local_port()
{
  switch (get_local_addr().ss_family) {
  case AF_INET:
    return ntohs(((struct sockaddr_in *)&(get_local_addr()))->sin_port);
  case AF_INET6:
    return ntohs(((struct sockaddr_in6 *)&(get_local_addr()))->sin6_port);
  default:
    return 0;
  }
}
