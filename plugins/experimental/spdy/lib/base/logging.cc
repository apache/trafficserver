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

#include "logging.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

template <>
std::string
stringof<struct sockaddr>(const sockaddr &sa)
{
  char buf[INET6_ADDRSTRLEN + 1];
  union {
    const in_addr *in;
    const in6_addr *in6;
  } ptr;

  ptr.in = nullptr;
  switch (sa.sa_family) {
  case AF_INET:
    ptr.in = &((const sockaddr_in *)(&sa))->sin_addr;
    break;
  case AF_INET6:
    ptr.in6 = &((const sockaddr_in6 *)(&sa))->sin6_addr;
    break;
  }

  inet_ntop(sa.sa_family, ptr.in, buf, sizeof(buf));
  return std::string(buf);
}

/* vim: set sw=4 ts=4 tw=79 et : */
