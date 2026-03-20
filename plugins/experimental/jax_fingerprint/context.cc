/** @file

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

#include "plugin.h"
#include "context.h"

JAxContext::JAxContext(const char *method_name, sockaddr const *s_sockaddr) : _method_name(method_name)
{
  _addr[0] = '\0';

  if (s_sockaddr == nullptr) {
    return;
  }

  switch (s_sockaddr->sa_family) {
  case AF_INET:
    inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(s_sockaddr)->sin_addr, _addr, INET_ADDRSTRLEN);
    break;
  case AF_INET6:
    inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6 *>(s_sockaddr)->sin6_addr, _addr, INET6_ADDRSTRLEN);
    break;
  case AF_UNIX:
    strncpy(_addr, reinterpret_cast<const sockaddr_un *>(s_sockaddr)->sun_path, sizeof(_addr) - 1);
    _addr[sizeof(_addr) - 1] = '\0';
    break;
  default:
    break;
  }
  Dbg(dbg_ctl, "New context for %s (%p) has been created", this->_method_name, this);
}

JAxContext::~JAxContext()
{
  Dbg(dbg_ctl, "Context for %s (%p) has been deleted", this->_method_name, this);
}

const std::string &
JAxContext::get_fingerprint() const
{
  return this->_fingerprint;
}

void
JAxContext::set_fingerprint(const std::string &fingerprint)
{
  this->_fingerprint = fingerprint;
  Dbg(dbg_ctl, "Fingerprint: %s", this->_fingerprint.c_str());
}

const char *
JAxContext::get_addr() const
{
  return this->_addr;
}

const char *
JAxContext::get_method_name() const
{
  return this->_method_name;
}
