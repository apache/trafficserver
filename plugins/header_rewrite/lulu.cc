/*
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

#include <string>

#include "ts/ts.h"
#include "lulu.h"

// Helper function to cleanly get the IP as a string.
char*
getIP(sockaddr const* s_sockaddr, char res[INET6_ADDRSTRLEN])
{
  res[0] = '\0';

  if (s_sockaddr == NULL) {
    return NULL;
  }

  switch (s_sockaddr->sa_family) {
  case AF_INET:
    {
      const struct sockaddr_in *s_sockaddr_in = reinterpret_cast<const struct sockaddr_in *>(s_sockaddr);
      inet_ntop(AF_INET, &s_sockaddr_in->sin_addr, res, INET_ADDRSTRLEN);
    }
    break;
  case AF_INET6:
    {
      const struct sockaddr_in6 *s_sockaddr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(s_sockaddr);
      inet_ntop(AF_INET6, &s_sockaddr_in6->sin6_addr, res, INET6_ADDRSTRLEN);
    }
    break;
  }

  return res[0] ? res : NULL;
}

// Return it as a std::string instead (more expensive, but sometimes convenient)
std::string
getIP(sockaddr const* s_sockaddr)
{
  char res[INET6_ADDRSTRLEN] = { '\0' };

  if (getIP(s_sockaddr, res)) {
    return res;
  }

  return "";
}
