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



// This is total BS, because our libraries are riddled with cross dependencies.
// TODO: Clean up the dependency mess, and get rid of this.

#include "libts.h"
#include "LogObject.h"

#if defined(solaris)
#include <sys/types.h>
#include <unistd.h>
#endif


#include "P_Net.h"

int cache_config_mutex_retry_delay = 2;

DNSConnection::Options const DNSConnection::DEFAULT_OPTIONS;

int fds_limit = 8000;
UDPNetProcessor& udpNet; // = udpNetInternal;

ClassAllocator<UDPPacketInternal> udpPacketAllocator("udpPacketAllocator");

void
UDPConnection::Release()
{
  ink_release_assert(false);
}

void
UDPNetProcessor::FreeBandwidth(Continuation * udpConn)
{
  NOWARN_UNUSED(udpConn);
  ink_release_assert(false);
}

NetProcessor& netProcessor; //  = unix_netProcessor;

Action *
UnixNetProcessor::connect_re_internal(Continuation * cont, sockaddr const* target,  NetVCOptions * opt)
{
  NOWARN_UNUSED(cont);
  NOWARN_UNUSED(target);
  NOWARN_UNUSED(opt);
  ink_release_assert(false);
  return NULL;
}


#include "InkAPIInternal.h"
ConfigUpdateCbTable *global_config_cbs = NULL;

void
ConfigUpdateCbTable::invoke(const char *name)
{
  NOWARN_UNUSED(name);
  ink_release_assert(false);
}

const char *
event_int_to_string(int event, int blen, char *buffer)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(blen);
  NOWARN_UNUSED(buffer);
  ink_release_assert(false);
  return NULL;
}

struct Machine {  static Machine* instance(); };
Machine* Machine::instance() {
  ink_release_assert(false);
  return NULL;
}

#include "LogCollationAccept.h"
LogCollationAccept::LogCollationAccept(int port)
  : Continuation(new_ProxyMutex()),
    m_port(port),
    m_pending_event(NULL)
{
}
LogCollationAccept::~LogCollationAccept()
{
}

//
//int
//LogHost::write(LogBuffer * lb)
//{
//  NOWARN_UNUSED(lb);
//  ink_release_assert(false);
//  return 0;
//}

#include "LogCollationClientSM.h"
LogCollationClientSM::LogCollationClientSM(LogHost * log_host):
  Continuation(new_ProxyMutex()),
  m_host_vc(NULL),
  m_host_vio(NULL),
  m_auth_buffer(NULL),
  m_auth_reader(NULL),
  m_send_buffer(NULL),
  m_send_reader(NULL),
  m_pending_action(NULL),
  m_pending_event(NULL),
  m_abort_vio(NULL),
  m_abort_buffer(NULL),
  m_buffer_send_list(NULL), m_buffer_in_iocore(NULL), m_flow(LOG_COLL_FLOW_ALLOW), m_log_host(log_host), m_id(0)
{
  Debug("log-coll", "[%d]client::constructor", m_id);
}

LogCollationClientSM::~LogCollationClientSM()
{
}

int
LogCollationClientSM::send(LogBuffer * log_buffer)
{
  NOWARN_UNUSED(log_buffer);
  ink_release_assert(false);
  return 0;
}


// TODO: The following was necessary only for Solaris, should examine more.
NetVCOptions const Connection::DEFAULT_OPTIONS;
NetProcessor::AcceptOptions const NetProcessor::DEFAULT_ACCEPT_OPTIONS;

// TODO: This is even uglier, this actually gets called here when "defined".
NetProcessor::AcceptOptions&
NetProcessor::AcceptOptions::reset()
{
  port = 0;
  accept_threads = 0;
  domain = AF_INET;
  etype = ET_NET;
  f_callback_on_open = false;
  recv_bufsize = 0;
  send_bufsize = 0;
  sockopt_flags = 0;
  f_outbound_transparent = false;
  f_inbound_transparent = false;
  return *this;
}

int
net_accept(NetAccept * na, void *ep, bool blockable)
{
  NOWARN_UNUSED(na);
  NOWARN_UNUSED(ep);
  NOWARN_UNUSED(blockable);
  ink_release_assert(false);
  return 0;
}


// These are for clang / llvm

int
CacheVC::handleWrite(int event, Event *e)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(e);
  return 0;
  ink_release_assert(false);
}
