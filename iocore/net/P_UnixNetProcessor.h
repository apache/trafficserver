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

#ifndef __UNIXNETPROCESSOR_H__
#define __UNIXNETPROCESSOR_H__
#include "I_Net.h"
#include "P_NetAccept.h"

class UnixNetVConnection;

//////////////////////////////////////////////////////////////////
//
//  class UnixNetProcessor
//
//////////////////////////////////////////////////////////////////
struct UnixNetProcessor:public NetProcessor
{
public:

  virtual Action * accept_internal(Continuation * cont,
                                   int fd,
                                   sockaddr * bound_sockaddr = NULL,
                                   int *bound_sockaddr_size = NULL,
                                   bool frequent_accept = true,
                                   AcceptFunctionPtr fn = net_accept,
                                   unsigned int accept_ip = INADDR_ANY,
				   AcceptOptions const& opt = DEFAULT_ACCEPT_OPTIONS
				   );


  Action *connect_re_internal(Continuation * cont,
                              unsigned int ip, int port, NetVCOptions * options = NULL);

  Action *connect(Continuation * cont,
                  UnixNetVConnection ** vc,
                  unsigned int ip, int port, NetVCOptions * opt = NULL);

  // Virtual function allows etype
  // to be set to ET_SSL for SSLNetProcessor.  Does
  // nothing for NetProcessor
  virtual void setEtype(EventType & etype)
  {
    (void) etype;
  };
  // Functions all THREAD_FREE and THREAD_ALLOC to be performed
  // for both SSL and regular NetVConnection transparent to
  // netProcessor connect functions.
  virtual UnixNetVConnection *allocateThread(EThread * t);
  virtual void freeThread(UnixNetVConnection * vc, EThread * t);
  virtual NetAccept *createNetAccept();

  virtual int start(int number_of_net_threads = 0 /* uses event threads */ );

  char *throttle_error_message;
  Event *accept_thread_event;

  // offsets for per thread data structures
  off_t netHandler_offset;
  off_t pollCont_offset;

  // we probably wont need these members
  int n_netthreads;
  EThread **netthreads;

  char *incoming_ip_to_bind;
  int incoming_ip_to_bind_saddr;
};


TS_INLINE Action *
NetProcessor::connect_re(Continuation * cont, unsigned int ip, int port, NetVCOptions * opts)
{
  return static_cast<UnixNetProcessor *>(this)->connect_re_internal(cont, ip, port, opts);
}


extern UnixNetProcessor unix_netProcessor;

//
// Set up a thread to receive events from the NetProcessor
// This function should be called for all threads created to
// accept such events by the EventProcesor.
//
extern void initialize_thread_for_net(EThread * thread, int thread_index);
#if defined(USE_OLD_EVENTFD)
extern void initialize_eventfd(EThread * thread);
#endif
//#include "UnixNet.h"
#endif
