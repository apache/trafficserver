/** @file

  NetEvent timer helpers: owner-thread-safe timer rearm and default inactivity deadline rebasing.

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

#include "iocore/net/NetEvent.h"
#include "iocore/net/NetHandler.h"
#include "P_Net.h"

bool
NetEvent::arm_default_inactivity_timeout()
{
  if (closed || next_inactivity_timeout_at != 0 || !(read.enabled || write.enabled)) {
    return false;
  }

  ink_hrtime const default_in = default_inactivity_timeout_in.load(std::memory_order_relaxed);

  if (default_in <= 0) {
    return false;
  }
  use_default_inactivity_timeout = true;
  inactivity_timeout_in          = 0;
  next_inactivity_timeout_at     = ink_get_hrtime() + default_in;
  Metrics::Counter::increment(net_rsb.default_inactivity_timeout_applied);
  rearm_timer();
  return true;
}

void
NetEvent::rearm_timer()
{
  NetHandler *const h = nh;

  if (h == nullptr) {
    return;
  }
  // The owning thread touches its wheel without holding the NetHandler mutex,
  // so the mutex does not protect it and a foreign thread must not take it.
  if (h->thread == this_ethread()) {
    h->rearm_timer(this);
  } else {
    h->defer_rearm(this);
  }
}

void
NetEvent::rebase_default_inactivity_deadline(ink_hrtime previous_in)
{
  if (next_inactivity_timeout_at == 0) {
    arm_default_inactivity_timeout();
    return;
  }
  if (!use_default_inactivity_timeout || inactivity_timeout_in != 0) {
    return; // the deadline did not come from the default
  }

  ink_hrtime const current_in = default_inactivity_timeout_in.load(std::memory_order_relaxed);

  if (current_in <= 0) {
    next_inactivity_timeout_at = 0;
  } else if (previous_in > 0) {
    next_inactivity_timeout_at += current_in - previous_in;
  } else {
    next_inactivity_timeout_at = ink_get_hrtime() + current_in;
  }
  rearm_timer();
}
