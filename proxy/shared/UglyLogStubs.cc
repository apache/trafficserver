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

#include "ts/ink_platform.h"
#include "LogObject.h"

#if defined(solaris)
#include <sys/types.h>
#include <unistd.h>
#endif

#include "P_Net.h"

int fds_limit = 8000;

class FakeUDPNetProcessor : public UDPNetProcessor
{
  virtual int
  start(int, size_t)
  {
    ink_release_assert(false);
    return 0;
  };
} fakeUDPNet;

UDPNetProcessor &udpNet = fakeUDPNet;

ClassAllocator<UDPPacketInternal> udpPacketAllocator("udpPacketAllocator");

void
UDPConnection::Release()
{
  ink_release_assert(false);
}

#include "InkAPIInternal.h"
ConfigUpdateCbTable *global_config_cbs = NULL;

void
ConfigUpdateCbTable::invoke(const char * /* name ATS_UNUSED */)
{
  ink_release_assert(false);
}

struct Machine {
  static Machine *instance();
};
Machine *
Machine::instance()
{
  ink_release_assert(false);
  return NULL;
}

#include "LogCollationAccept.h"
LogCollationAccept::LogCollationAccept(int port) : Continuation(new_ProxyMutex()), m_port(port), m_pending_event(NULL)
{
}
LogCollationAccept::~LogCollationAccept()
{
}

#include "LogCollationClientSM.h"
LogCollationClientSM::LogCollationClientSM(LogHost *log_host)
  : Continuation(new_ProxyMutex()),
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
    m_buffer_send_list(NULL),
    m_buffer_in_iocore(NULL),
    m_flow(LOG_COLL_FLOW_ALLOW),
    m_log_host(log_host),
    m_id(0)
{
}

LogCollationClientSM::~LogCollationClientSM()
{
}

int
LogCollationClientSM::send(LogBuffer * /* log_buffer ATS_UNUSED */)
{
  ink_release_assert(false);
  return 0;
}

NetAccept *
UnixNetProcessor::createNetAccept()
{
  ink_release_assert(false);
  return NULL;
}

// TODO: The following was necessary only for Solaris, should examine more.
NetVCOptions const Connection::DEFAULT_OPTIONS;
NetProcessor::AcceptOptions const NetProcessor::DEFAULT_ACCEPT_OPTIONS;
DNSConnection::Options const DNSConnection::DEFAULT_OPTIONS;

// TODO: This is even uglier, this actually gets called here when "defined".
NetProcessor::AcceptOptions &
NetProcessor::AcceptOptions::reset()
{
  local_port            = 0;
  accept_threads        = 0;
  ip_family             = AF_INET;
  etype                 = ET_NET;
  f_callback_on_open    = false;
  recv_bufsize          = 0;
  send_bufsize          = 0;
  sockopt_flags         = 0;
  packet_mark           = 0;
  packet_tos            = 0;
  f_inbound_transparent = false;
  return *this;
}

// These are for clang / llvm
int
CacheVC::handleWrite(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  return 0;
  ink_release_assert(false);
}

UnixNetProcessor unix_netProcessor;
NetProcessor &netProcessor = unix_netProcessor;

int
UnixNetProcessor::start(int, size_t)
{
  ink_release_assert(false);
  return 0;
}

Action *
NetProcessor::accept(Continuation * /* cont ATS_UNUSED */, AcceptOptions const & /* opt ATS_UNUSED */)
{
  ink_release_assert(false);
  return NULL;
}

Action *
NetProcessor::main_accept(Continuation * /* cont ATS_UNUSED */, SOCKET /* fd ATS_UNUSED */,
                          AcceptOptions const & /* opt ATS_UNUSED */)
{
  ink_release_assert(false);
  return NULL;
}

Action *
UnixNetProcessor::accept_internal(Continuation * /* cont ATS_UNUSED */, int /* fd ATS_UNUSED */,
                                  AcceptOptions const & /* opt ATS_UNUSED */)
{
  ink_release_assert(false);
  return NULL;
}

NetVConnection *
UnixNetProcessor::allocate_vc(EThread *)
{
  ink_release_assert(false);
  return NULL;
}

// For Intel ICC
int cache_config_mutex_retry_delay = 2;

#include "I_SplitDNSProcessor.h"
void
SplitDNSConfig::reconfigure()
{
}

ClassAllocator<CacheRemoveCont> cacheRemoveContAllocator("cacheRemoveCont");

CacheHostTable::CacheHostTable(Cache * /* c ATS_UNUSED */, CacheType /* typ ATS_UNUSED */)
{
}

CacheHostTable::~CacheHostTable()
{
}
