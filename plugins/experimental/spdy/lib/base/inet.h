/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INET_H_9452AE2E_D6D2_4B26_AF98_7550DC033E54
#define INET_H_9452AE2E_D6D2_4B26_AF98_7550DC033E54

#include <stdlib.h> // abort
#include <sys/types.h>
#include <netinet/in.h>

struct inet_address {
  explicit inet_address(const struct sockaddr *addr)
  {
    switch (addr->sa_family) {
    case AF_INET:
      memcpy(&sa.storage, addr, sizeof(sockaddr_in));
      break;
    case AF_INET6:
      memcpy(&sa.storage, addr, sizeof(sockaddr_in6));
      break;
    default:
      memset(&sa, 0, sizeof(sa));
    }
  }

  uint16_t &
  port()
  {
    switch (sa.storage.ss_family) {
    case AF_INET:
      return sa.in.sin_port;
    case AF_INET6:
      return sa.in6.sin6_port;
    default:
      TSError("invalid inet address type %u", sa.storage.ss_family);
      abort();
    }
  }

  const sockaddr *
  saddr() const
  {
    return &sa.sa;
  }

private:
  union {
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
    struct sockaddr sa;
    struct sockaddr_storage storage;
  } sa;
};

template <>
std::string
stringof<inet_address>(const inet_address &inaddr)
{
  return cstringof(*inaddr.saddr());
}

#endif /* INET_H_9452AE2E_D6D2_4B26_AF98_7550DC033E54 */
/* vim: set sw=4 ts=4 tw=79 et : */
