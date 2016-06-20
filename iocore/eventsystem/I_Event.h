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

#ifndef _Event_h_
#define _Event_h_

#include "ts/ink_platform.h"
#include "I_Action.h"

//
//  Defines
//

#define MAX_EVENTS_PER_THREAD 100000

// Events

#define EVENT_NONE CONTINUATION_EVENT_NONE // 0
#define EVENT_IMMEDIATE 1
#define EVENT_INTERVAL 2
#define EVENT_ERROR 3
#define EVENT_CALL 4 // used internally in state machines
#define EVENT_POLL 5 // negative event; activated on poll or epoll

// Event callback return functions

#define EVENT_DONE CONTINUATION_DONE // 0
#define EVENT_CONT CONTINUATION_CONT // 1
#define EVENT_RETURN 5
#define EVENT_RESTART 6
#define EVENT_RESTART_DELAYED 7

// Event numbers block allocation
// ** ALL NEW EVENT TYPES SHOULD BE ALLOCATED FROM BLOCKS LISTED HERE! **

#define VC_EVENT_EVENTS_START 100
#define NET_EVENT_EVENTS_START 200
#define DISK_EVENT_EVENTS_START 300
#define CLUSTER_EVENT_EVENTS_START 400
#define HOSTDB_EVENT_EVENTS_START 500
#define DNS_EVENT_EVENTS_START 600
#define CONFIG_EVENT_EVENTS_START 800
#define LOG_EVENT_EVENTS_START 900
#define MULTI_CACHE_EVENT_EVENTS_START 1000
#define CACHE_EVENT_EVENTS_START 1100
#define CACHE_DIRECTORY_EVENT_EVENTS_START 1200
#define CACHE_DB_EVENT_EVENTS_START 1300
#define HTTP_NET_CONNECTION_EVENT_EVENTS_START 1400
#define HTTP_NET_VCONNECTION_EVENT_EVENTS_START 1500
#define GC_EVENT_EVENTS_START 1600
#define ICP_EVENT_EVENTS_START 1800
#define TRANSFORM_EVENTS_START 2000
#define STAT_PAGES_EVENTS_START 2100
#define HTTP_SESSION_EVENTS_START 2200
#define HTTP2_SESSION_EVENTS_START 2250
#define HTTP_TUNNEL_EVENTS_START 2300
#define HTTP_SCH_UPDATE_EVENTS_START 2400
#define NT_ASYNC_CONNECT_EVENT_EVENTS_START 3000
#define NT_ASYNC_IO_EVENT_EVENTS_START 3100
#define RAFT_EVENT_EVENTS_START 3200
#define SIMPLE_EVENT_EVENTS_START 3300
#define UPDATE_EVENT_EVENTS_START 3500
#define LOG_COLLATION_EVENT_EVENTS_START 3800
#define AIO_EVENT_EVENTS_START 3900
#define BLOCK_CACHE_EVENT_EVENTS_START 4000
#define UTILS_EVENT_EVENTS_START 5000
#define CONGESTION_EVENT_EVENTS_START 5100
#define INK_API_EVENT_EVENTS_START 60000
#define SRV_EVENT_EVENTS_START 62000
#define REMAP_EVENT_EVENTS_START 63000

// define misc events here
#define ONE_WAY_TUNNEL_EVENT_PEER_CLOSE (SIMPLE_EVENT_EVENTS_START + 1)
#define PREFETCH_EVENT_SEND_URL (SIMPLE_EVENT_EVENTS_START + 2)

typedef int EventType;
const int ET_CALL         = 0;
const int MAX_EVENT_TYPES = 8; // conservative, these are dynamically allocated

class EThread;

/**
  A type of Action returned by the EventProcessor. The Event class
  is the type of Action returned by the EventProcessor as a result
  of scheduling an operation. Unlike asynchronous operations
  represented by actions, events never call reentrantly.

  Besides being able to cancel an event (because it is an action),
  you can also reschedule it once received.

  <b>Remarks</b>

  When reschedulling an event through any of the Event class
  schedulling fuctions, state machines must not make these calls
  in other thread other than the one that called them back. They
  also must have acquired the continuation's lock before calling
  any of the schedulling functions.

  The rules for cancelling an event are the same as those for
  actions:

  The canceller of an event must be the state machine that will be
  called back by the task and that state machine's lock must be
  held while calling cancel. Any reference to that event object
  (ie. pointer) held by the state machine must not be used after
  the cancellation.

  Event Codes:

  At the completion of an event, state machines use the event code
  passed in through the Continuation's handler function to distinguish
  the type of event and handle the data parameter accordingly. State
  machine implementers should be careful when defining the event
  codes since they can impact on other state machines presents. For
  this reason, this numbers are usually allocated from a common
  pool.

  Time values:

  The schedulling functions use a time parameter typed as ink_hrtime
  for specifying the timeouts or periods. This is a nanosecond value
  supported by libts and you should use the time functions and
  macros defined in ink_hrtime.h.

  The difference between the timeout specified for schedule_at and
  schedule_in is that in the former it is an absolute value of time
  that is expected to be in the future where in the latter it is
  an amount of time to add to the current time (obtained with
  ink_get_hrtime).

*/
class Event : public Action
{
public:
  ///////////////////////////////////////////////////////////
  // Common Interface                                      //
  ///////////////////////////////////////////////////////////

  /**
     Reschedules this event immediately. Instructs the event object
     to reschedule itself as soon as possible in the EventProcessor.

     @param callback_event Event code to return at the completion
      of this event. See the Remarks section.

  */
  void schedule_imm(int callback_event = EVENT_IMMEDIATE);

  /**
     Reschedules this event to callback at time 'atimeout_at'.
     Instructs the event object to reschedule itself at the time
     specified in atimeout_at on the EventProcessor.

     @param atimeout_at Time at which to callcallback. See the Remarks section.
     @param callback_event Event code to return at the completion of this event. See the Remarks section.

  */
  void schedule_at(ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL);

  /**
     Reschedules this event to callback at time 'atimeout_at'.
     Instructs the event object to reschedule itself at the time
     specified in atimeout_at on the EventProcessor.

     @param atimeout_in Time at which to callcallback. See the Remarks section.
     @param callback_event Event code to return at the completion of this event. See the Remarks section.

  */
  void schedule_in(ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL);

  /**
     Reschedules this event to callback every 'aperiod'. Instructs
     the event object to reschedule itself to callback every 'aperiod'
     from now.

     @param aperiod Time period at which to callcallback. See the Remarks section.
     @param callback_event Event code to return at the completion of this event. See the Remarks section.

  */
  void schedule_every(ink_hrtime aperiod, int callback_event = EVENT_INTERVAL);

  // inherited from Action::cancel
  // virtual void cancel(Continuation * c = NULL);

  void free();

  EThread *ethread;

  unsigned int in_the_prot_queue : 1;
  unsigned int in_the_priority_queue : 1;
  unsigned int immediate : 1;
  unsigned int globally_allocated : 1;
  unsigned int in_heap : 4;
  int callback_event;

  ink_hrtime timeout_at;
  ink_hrtime period;

  /**
    This field can be set when an event is created. It is returned
    as part of the Event structure to the continuation when handleEvent
    is called.

  */
  void *cookie;

  // Private

  Event();

  Event *init(Continuation *c, ink_hrtime atimeout_at = 0, ink_hrtime aperiod = 0);

#ifdef ENABLE_TIME_TRACE
  ink_hrtime start_time;
#endif

private:
  void *operator new(size_t size); // use the fast allocators

private:
  // prevent unauthorized copies (Not implemented)
  Event(const Event &);
  Event &operator=(const Event &);

public:
  LINK(Event, link);

/*-------------------------------------------------------*\
| UNIX/non-NT Interface                                   |
\*-------------------------------------------------------*/

#ifdef ONLY_USED_FOR_FIB_AND_BIN_HEAP
  void *node_pointer;
  void
  set_node_pointer(void *x)
  {
    node_pointer = x;
  }
  void *
  get_node_pointer()
  {
    return node_pointer;
  }
#endif

#if defined(__GNUC__)
  virtual ~Event() {}
#endif
};

//
// Event Allocator
//
extern ClassAllocator<Event> eventAllocator;

#define EVENT_ALLOC(_a, _t) THREAD_ALLOC(_a, _t)
#define EVENT_FREE(_p, _a, _t) \
  _p->mutex = NULL;            \
  if (_p->globally_allocated)  \
    ::_a.free(_p);             \
  else                         \
  THREAD_FREE(_p, _a, _t)

#endif /*_Event_h_*/
