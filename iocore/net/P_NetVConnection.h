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

TS_INLINE sockaddr const*
NetVConnection::get_remote_addr()
{
  if (!got_remote_addr) {
    set_remote_addr();
    got_remote_addr = 1;
  }
  return ink_inet_sa_cast(&remote_addr);
}

TS_INLINE unsigned int
NetVConnection::get_remote_ip()
{
  sockaddr const* addr = this->get_remote_addr();
  return ink_inet_is_ip4(addr)
    ? ink_inet_ip4_addr_cast(addr)
    : 0;
}


/// @return The remote port in host order.
TS_INLINE int
NetVConnection::get_remote_port()
{
  return ink_inet_get_port(this->get_remote_addr());
}

TS_INLINE sockaddr const*
NetVConnection::get_local_addr()
{
  if (!got_local_addr) {
    set_local_addr();
    sockaddr* a = ink_inet_sa_cast(&local_addr); // cache required type.
    if (
      (ink_inet_is_ip(a) && ink_inet_port_cast(a)) // IP and has a port.
      || (ink_inet_is_ip4(a) && ink_inet_ip4_addr_cast(a)) // IPv4
      || (ink_inet_is_ip6(a) && !IN6_IS_ADDR_UNSPECIFIED(&ink_inet_ip6_addr_cast(a)))
    ) {
      got_local_addr = 1;
    }
  }
  return ink_inet_sa_cast(&local_addr);
}

TS_INLINE unsigned int
NetVConnection::get_local_ip()
{
  sockaddr const* addr = this->get_local_addr();
  return ink_inet_is_ip4(addr)
    ? ink_inet_ip4_addr_cast(addr)
    : 0;
}

/// @return The local port in host order.
TS_INLINE int
NetVConnection::get_local_port()
{
  return ink_inet_get_port(this->get_local_addr());
}
