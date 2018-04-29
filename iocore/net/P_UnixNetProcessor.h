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

#pragma once
#include "I_Net.h"
#include "P_NetAccept.h"

class UnixNetVConnection;

//////////////////////////////////////////////////////////////////
//
//  class UnixNetProcessor
//
//////////////////////////////////////////////////////////////////
struct UnixNetProcessor : public NetProcessor {
public:
  virtual Action *accept_internal(Continuation *cont, int fd, AcceptOptions const &opt);

  Action *connect_re_internal(Continuation *cont, sockaddr const *target, NetVCOptions *options = nullptr);
  Action *connect(Continuation *cont, UnixNetVConnection **vc, sockaddr const *target, NetVCOptions *opt = nullptr);

  virtual NetAccept *createNetAccept(const NetProcessor::AcceptOptions &opt);
  NetVConnection *allocate_vc(EThread *t) override;

  void init() override;

  Event *accept_thread_event;

  // offsets for per thread data structures
  off_t netHandler_offset;
  off_t pollCont_offset;

  // we probably wont need these members
  int n_netthreads;
  EThread **netthreads;
};

TS_INLINE Action *
NetProcessor::connect_re(Continuation *cont, sockaddr const *addr, NetVCOptions *opts)
{
  return static_cast<UnixNetProcessor *>(this)->connect_re_internal(cont, addr, opts);
}

extern UnixNetProcessor unix_netProcessor;

//
// Set up a thread to receive events from the NetProcessor
// This function should be called for all threads created to
// accept such events by the EventProcesor.
//
extern void initialize_thread_for_net(EThread *thread);

//#include "UnixNet.h"
