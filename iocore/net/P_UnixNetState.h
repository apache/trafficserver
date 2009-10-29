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

#ifdef INKIO_NET_DBG

struct StateInfo
{
  int nbytes;                   /* no of bytes written/read */
  int next_op_bytes;            /* no of bytes to be written/read in the next op */
  int last_op_bytes;            /* no of bytes written/read in the previous op */
  int n_completed_ops;          /* no of ops that have been successfully completed */
  int n_scheduled_ops;          /* no of ops that have been scheduled */
  ink_hrtime prev_op_setup;     /* time when last op was issued */
  ink_hrtime prev_op_compl;     /* time when last op was completed */
  int state_id;                 /* a monotonic number that is incremented every time
                                   the info is changed */

    StateInfo():nbytes(0), next_op_bytes(0), last_op_bytes(0),
    n_completed_ops(0), n_scheduled_ops(0), prev_op_setup(0), prev_op_compl(0), state_id(0)
  {
  }

  void reset()
  {
    nbytes = 0;
    next_op_bytes = 0;
    last_op_bytes = 0;
    n_completed_ops = 0;
    n_scheduled_ops = 0;
    state_id = 0;
    prev_op_setup = 0;
    prev_op_compl = 0;
  }

  void check_final()
  {
    ink_debug_assert(nbytes >= last_op_bytes);
    //ink_debug_assert(n_scheduled_ops == n_completed_ops);
  }

  void update_on_setup(int bytes)
  {
    prev_op_setup = ink_get_hrtime_internal();
    ink_debug_assert(nbytes >= last_op_bytes);
    ink_debug_assert(n_scheduled_ops == n_completed_ops);
    n_scheduled_ops++;
    ink_debug_assert(next_op_bytes == 0);
    next_op_bytes += bytes;
    state_id++;
  }

  void update_on_completion(int bytes)
  {
    prev_op_compl = ink_get_hrtime_internal();
    ink_debug_assert(nbytes >= last_op_bytes);
    n_completed_ops++;
    ink_debug_assert(n_scheduled_ops == n_completed_ops);
    ink_debug_assert(bytes <= next_op_bytes);
    nbytes += bytes;
    last_op_bytes = bytes;
    next_op_bytes = 0;
    state_id++;
  }

};

#endif

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

#ifdef NET_DUMP_STATS
  NetDump dump[NET_DUMP_SIZE];
  int dump_size;
#endif

#ifdef INKIO_NET_DBG
  StateInfo info;
#endif

  void enqueue(void *q, UnixNetVConnection * vc);
    NetState();
};
#endif
