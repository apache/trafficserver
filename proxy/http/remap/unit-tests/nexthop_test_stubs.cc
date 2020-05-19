/** @file

  nexthop unit test stubs for unit testing nexthop strategies.

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

  Implements code necessary for Reverse Proxy which mostly consists of
  general purpose hostname substitution in URLs.

 */

#include "nexthop_test_stubs.h"

#include "HttpTransact.h"

void
br(HttpRequestData *h, const char *os_hostname, sockaddr const *dest_ip)
{
  HdrHeap *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  h->hdr        = new HTTPHdr();
  h->hdr->create(HTTP_TYPE_REQUEST, heap);
  h->hostname_str = ats_strdup(os_hostname);
  h->xact_start   = time(nullptr);
  ink_zero(h->src_ip);
  ink_zero(h->dest_ip);
  ats_ip_copy(&h->dest_ip.sa, dest_ip);
  h->incoming_port = 80;
  h->api_info      = new HttpApiInfo();
}

void
br_reinit(HttpRequestData *h)
{
  if (h->hdr) {
    ats_free(h->hdr);
    ats_free(h->hostname_str);
    if (h->hdr) {
      delete h->hdr;
    }
    if (h->api_info) {
      delete h->api_info;
    }
  }
}

void
PrintToStdErr(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}

char *
HttpRequestData::get_string()
{
  return nullptr;
}
const char *
HttpRequestData::get_host()
{
  return nullptr;
}
sockaddr const *
HttpRequestData::get_ip()
{
  return nullptr;
}

sockaddr const *
HttpRequestData::get_client_ip()
{
  return nullptr;
}

#include "InkAPIInternal.h"
void
ConfigUpdateCbTable::invoke(char const *p)
{
}

#include "I_Machine.h"

static Machine *my_machine = nullptr;

Machine::Machine(char const *hostname, sockaddr const *addr) {}
Machine::~Machine()
{
  delete my_machine;
}
Machine *
Machine::instance()
{
  if (my_machine == nullptr) {
    my_machine = new Machine(nullptr, nullptr);
  }
  return my_machine;
}
bool
Machine::is_self(const char *name)
{
  return false;
}

#include "HostStatus.h"

HostStatRec::HostStatRec(){};
HostStatus::HostStatus() {}
HostStatus::~HostStatus(){};
HostStatRec *
HostStatus::getHostStatus(const char *name)
{
  // for unit tests only, always return a record with HOST_STATUS_UP
  static HostStatRec rec;
  rec.status = HostStatus_t::HOST_STATUS_UP;
  return &rec;
}
void
HostStatus::setHostStatus(char const *host, HostStatus_t status, unsigned int, unsigned int)
{
  NH_Debug("next_hop", "setting host status for '%s' to %s", host, HostStatusNames[status]);
}

#include "I_UDPConnection.h"

void
UDPConnection::Release()
{
}

#include "P_UDPPacket.h"
inkcoreapi ClassAllocator<UDPPacketInternal> udpPacketAllocator("udpPacketAllocator");
// for UDPPacketInternal::free()
