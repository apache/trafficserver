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

#include "HttpSM.h"
#include "nexthop_test_stubs.h"

HttpSM::HttpSM() : Continuation(nullptr), vc_table(this) {}
void
HttpSM::cleanup()
{
}
void
HttpSM::destroy()
{
}
void
HttpSM::handle_api_return()
{
}
void
HttpSM::set_next_state()
{
}

HttpVCTable::HttpVCTable(HttpSM *smp)
{
  sm = smp;
}
HttpCacheAction::HttpCacheAction() {}
void
HttpCacheAction::cancel(Continuation *c)
{
}
PostDataBuffers::~PostDataBuffers() {}
void
APIHooks::clear()
{
}

HttpTunnel::HttpTunnel() {}
HttpCacheSM::HttpCacheSM() {}
HttpHookState::HttpHookState() {}
HttpTunnelConsumer::HttpTunnelConsumer() {}
HttpTunnelProducer::HttpTunnelProducer() {}
ChunkedHandler::ChunkedHandler() {}

alignas(OverridableHttpConfigParams) char _my_txn_conf[sizeof(OverridableHttpConfigParams)];

// this is done to cleanup and avoid memory leaks in the unit tests.
static HdrHeap *myHeap = nullptr;
void
br_destroy(HttpSM &sm)
{
  HttpRequestData *h = &sm.t_state.request_data;
  if (myHeap != nullptr) {
    myHeap->destroy();
    myHeap = nullptr;
  }
  delete h->hdr;
  delete h->api_info;
  ats_free(h->hostname_str);
}

void
build_request(int64_t sm_id, HttpSM *sm, sockaddr_in *ip, const char *os_hostname, sockaddr const *dest_ip)
{
  sm->sm_id = sm_id;

  if (myHeap == nullptr) {
    myHeap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  }
  if (sm->t_state.request_data.hdr != nullptr) {
    delete sm->t_state.request_data.hdr;
  }
  sm->t_state.request_data.hdr = new HTTPHdr();
  sm->t_state.request_data.hdr->create(HTTP_TYPE_REQUEST, myHeap);
  if (sm->t_state.request_data.hostname_str != nullptr) {
    ats_free(sm->t_state.request_data.hostname_str);
  }
  sm->t_state.request_data.hostname_str = ats_strdup(os_hostname);
  sm->t_state.request_data.xact_start   = time(nullptr);
  ink_zero(sm->t_state.request_data.src_ip);
  ink_zero(sm->t_state.request_data.dest_ip);
  ats_ip_copy(&sm->t_state.request_data.dest_ip.sa, dest_ip);
  sm->t_state.request_data.incoming_port = 80;
  if (sm->t_state.request_data.api_info != nullptr) {
    delete sm->t_state.request_data.api_info;
  }
  sm->t_state.request_data.api_info = new HttpApiInfo();
  if (ip != nullptr) {
    memcpy(&sm->t_state.request_data.src_ip.sa, ip, sizeof(sm->t_state.request_data.src_ip.sa));
  }
  sm->t_state.request_data.xact_start = time(0);

  memset(_my_txn_conf, 0, sizeof(_my_txn_conf));
  OverridableHttpConfigParams *oride = reinterpret_cast<OverridableHttpConfigParams *>(_my_txn_conf);
  oride->parent_retry_time           = 1;
  oride->parent_fail_threshold       = 1;
  sm->t_state.txn_conf               = reinterpret_cast<OverridableHttpConfigParams *>(_my_txn_conf);
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
  return &src_ip.sa;
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

HostStatus::~HostStatus()
{
  for (auto i = this->hosts_statuses.begin(); i != this->hosts_statuses.end(); ++i) {
    delete i->second;
  }
}

HostStatRec *
HostStatus::getHostStatus(const char *name)
{
  if (this->hosts_statuses[name] == nullptr) {
    // for unit tests only, always return a record with HOST_STATUS_UP, if it wasn't set with setHostStatus
    static HostStatRec rec;
    rec.status = HostStatus_t::HOST_STATUS_UP;
    return &rec;
  }
  return this->hosts_statuses[name];
}

void
HostStatus::setHostStatus(char const *host, HostStatus_t status, unsigned int down_time, unsigned int reason)
{
  if (this->hosts_statuses[host] == nullptr) {
    this->hosts_statuses[host] = new (HostStatRec);
  }
  this->hosts_statuses[host]->status          = status;
  this->hosts_statuses[host]->reasons         = reason;
  this->hosts_statuses[host]->local_down_time = down_time;
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
