/** @file

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

#include <atomic>

#include "I_EventSystem.h"

class AtomicEvent
{
public:
  bool
  schedule(Continuation *c, EThread *t, int event, void *data, ink_hrtime delay = 0, int periodic = 0)
  {
    auto new_e = ::eventAllocator.alloc();
    new_e->init(c, delay, periodic);
    new_e->callback_event = event;
    new_e->cookie         = data;

    Event *tmp = nullptr;
    if (this->_e.compare_exchange_weak(tmp, new_e, std::memory_order_acq_rel)) {
      t->schedule(new_e);
      return true;
    } else {
      // we should not reschedule events when event is -1 or nullptr. Because the connection
      // might be closed or already have events in plane.
      new_e->free();
      return false;
    }
  }

  // thread unsafe. only target thread can cancel this event.
  void
  cancel()
  {
    Event *tmp = nullptr;
    do {
      if (tmp != nullptr) {
        tmp->cancel();
      }
      tmp = this->_e.load(std::memory_order_acquire);
      if (tmp == reinterpret_cast<Event *>(-1)) {
        return;
      }
    } while (!this->_e.compare_exchange_weak(tmp, static_cast<Event *>(nullptr), std::memory_order_acq_rel));

    if (tmp != nullptr) {
      tmp->cancel();
    }
  }

  void
  close()
  {
    Event *tmp = nullptr;
    do {
      if (tmp != nullptr) {
        tmp->cancel();
      }

      tmp = this->_e.load(std::memory_order_acquire);
      ink_release_assert(tmp != reinterpret_cast<Event *>(-1));
    } while (!this->_e.compare_exchange_weak(tmp, reinterpret_cast<Event *>(-1), std::memory_order_acq_rel));

    if (tmp != nullptr) {
      tmp->cancel();
    }
  }

private:
  std::atomic<Event *> _e{};
};
