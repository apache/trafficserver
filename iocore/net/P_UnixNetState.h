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

/****************************************************************************

  UnixNetState.h

  
   NetState
  
   State information for a particular channel of a NetVConnection
   This information is private to the Net module.   It is only here
   because of the the C++ compiler needs it to define NetVConnection.
  
   Shared with Cluster.cc
  

  
 ****************************************************************************/
#if !defined (_UnixNetState_h_)
#define _UnixNetState_h_

#include "List.h"
#include "I_VIO.h"

struct Event;
struct UnixNetVConnection;

struct NetState
{
  volatile int enabled;
  int priority;
  VIO vio;
  void *queue;
  void *netready_queue;         //added by YTS Team, yamsat
  void *enable_queue;           //added by YTS Team, yamsat
  int ifd;
  ink_hrtime do_next_at;
  Link<UnixNetVConnection> link;
  Link<UnixNetVConnection> netready_link;  //added by YTS Team, yamsat
  Link<UnixNetVConnection> enable_link;    //added by YTS Team, yamsat
  ink32 next_vc;
  int npending_scheds;

  int triggered;                // added by YTS Team, yamsat

  void enqueue(void *q, UnixNetVConnection * vc);

  NetState();
};
#endif
