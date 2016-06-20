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

#ifndef _P_UnixEvent_h_
#define _P_UnixEvent_h_

TS_INLINE Event *
Event::init(Continuation *c, ink_hrtime atimeout_at, ink_hrtime aperiod)
{
  continuation = c;
  timeout_at   = atimeout_at;
  period       = aperiod;
  immediate    = !period && !atimeout_at;
  cancelled    = false;
  return this;
}

TS_INLINE void
Event::free()
{
  mutex = NULL;
  eventAllocator.free(this);
}

TS_INLINE
Event::Event()
  : ethread(0),
    in_the_prot_queue(false),
    in_the_priority_queue(false),
    immediate(false),
    globally_allocated(true),
    in_heap(false),
    timeout_at(0),
    period(0)
{
}

#endif /*_UnixEvent_h_*/
