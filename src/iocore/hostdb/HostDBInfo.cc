/** @file

  HostDBInfo implementation

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

#include <sys/socket.h>

#include "iocore/hostdb/HostDBProcessor.h"

namespace
{
/** Assign raw storage to an @c IpAddr
 *
 * @param ip Destination.
 * @param af IP family.
 * @param ptr Raw data for an address of family @a af.
 */
void
ip_addr_set(IpAddr &ip,     ///< Target storage.
            uint8_t af,     ///< Address format.
            void const *ptr ///< Raw address data
)
{
  if (AF_INET6 == af) {
    ip = *static_cast<in6_addr const *>(ptr);
  } else if (AF_INET == af) {
    ip = *static_cast<in_addr_t const *>(ptr);
  } else {
    ip.invalidate();
  }
}
} // namespace

auto
HostDBInfo::assign(sa_family_t af, void const *addr) -> self_type &
{
  type = HostDBType::ADDR;
  ip_addr_set(data.ip, af, addr);
  return *this;
}

auto
HostDBInfo::assign(IpAddr const &addr) -> self_type &
{
  type    = HostDBType::ADDR;
  data.ip = addr;
  return *this;
}

auto
HostDBInfo::assign(SRV const *srv, char const *name) -> self_type &
{
  type                  = HostDBType::SRV;
  data.srv.srv_weight   = srv->weight;
  data.srv.srv_priority = srv->priority;
  data.srv.srv_port     = srv->port;
  data.srv.key          = srv->key;
  data.srv.srv_offset   = reinterpret_cast<char const *>(this) - name;
  return *this;
}

char const *
HostDBInfo::srvname() const
{
  return data.srv.srv_offset ? reinterpret_cast<char const *>(this) + data.srv.srv_offset : nullptr;
}
