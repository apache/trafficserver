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

int fds_limit = 8000;
UDPNetProcessor &udpNet;

ClassAllocator<UDPPacketInternal> udpPacketAllocator("udpPacketAllocator");

void
UDPConnection::Release()
{
  ink_release_assert(false);
}

void
UDPNetProcessor::FreeBandwidth(Continuation * udpConn)
{
  ink_release_assert(false);
}

NetProcessor& netProcessor;

Action *
UnixNetProcessor::connect_re_internal(Continuation * cont, unsigned int ip, int port,  NetVCOptions * opt)
{
  ink_release_assert(false);
  return NULL;
}


#include "InkAPIInternal.h"
ConfigUpdateCbTable *global_config_cbs = NULL;

void
ConfigUpdateCbTable::invoke(const char *name)
{
  ink_release_assert(false);
}

const char *
event_int_to_string(int event, int blen, char *buffer)
{
  ink_release_assert(false);
  return NULL;
}


struct Machine;
Machine *
this_machine()
{
  ink_release_assert(false);
  return NULL;
}


#include "LogConfig.h"
void
LogConfig::setup_collation(LogConfig * prev_config)
{
  ink_release_assert(false);
}

void
LogConfig::create_pre_defined_objects_with_filter(const PreDefinedFormatInfoList & pre_def_info_list, size_t num_filters,
                                                  LogFilter ** filter, const char *filt_name, bool force_extension)
{
  ink_release_assert(false);
}

int
LogHost::write(LogBuffer * lb, size_t * to_disk, size_t * to_net, size_t * to_pipe)
{
  ink_release_assert(false);
  return 0;
}


NetVCOptions const Connection::DEFAULT_OPTIONS;
NetProcessor::AcceptOptions const NetProcessor::DEFAULT_ACCEPT_OPTIONS;

NetProcessor::AcceptOptions&
NetProcessor::AcceptOptions::reset()
{
  ink_release_assert(false);
  return *this;
}
