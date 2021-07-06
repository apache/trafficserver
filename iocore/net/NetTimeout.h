/** @file

  NetTimeout

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

#include "tscore/List.h"
#include "tscore/ink_hrtime.h"

#include "I_EventSystem.h"

/**
  NetTimeout - handle active & inactive timeout
 */
class NetTimeout
{
public:
  void set_active_timeout(ink_hrtime timeout_in);
  void set_inactive_timeout(ink_hrtime timeout_in);
  void cancel_active_timeout();
  void cancel_inactive_timeout();
  void reset_active_timeout();
  void reset_inactive_timeout();
  bool is_active_timeout_expired(ink_hrtime now);
  bool is_inactive_timeout_expired(ink_hrtime now);
  ink_hrtime idle_time(ink_hrtime now);
  void update_inactivity();

private:
  ink_hrtime _active_timeout_in        = 0;
  ink_hrtime _inactive_timeout_in      = 0;
  ink_hrtime _next_active_timeout_at   = 0;
  ink_hrtime _next_inactive_timeout_at = 0;
};

/**
  ActivityCop - Check activity of T in the List in every @f seconds

  T have to handle VC_EVENT_ACTIVE_TIMEOUT and VC_EVENT_INACTIVITY_TIMEOUT events.

  TODO: add concepts like below with C++20
  ```
  template <class T, class List = DLL<T>>
  concept Timeoutable = requires(T *t, List *list, ink_hrtime time) {
    t->handleEvent();
    t->head;
    {list->next(t)} -> std::convertible_to<T *>;
    {t->is_active_timeout_expired(time)} -> std::same_as<bool>;
    {t->is_inactive_timeout_expired(time)} -> std::same_as<bool>;
  };
  ```
 */
template <class T, class List = DLL<T>> class ActivityCop : public Continuation
{
public:
  ActivityCop(){};
  ActivityCop(Ptr<ProxyMutex> &m, List *l, int f);

  void start();
  void stop();
  int check_activity(int event, Event *e);

private:
  Event *_event = nullptr;
  List *_list   = nullptr;
  int _freq     = 1;
};

////
// Inline functions

//
// NetTimeout
//
inline void
NetTimeout::set_active_timeout(ink_hrtime timeout_in)
{
  if (timeout_in == 0) {
    return;
  }

  _active_timeout_in      = timeout_in;
  _next_active_timeout_at = Thread::get_hrtime() + timeout_in;
}

inline void
NetTimeout::set_inactive_timeout(ink_hrtime timeout_in)
{
  if (timeout_in == 0) {
    return;
  }

  _inactive_timeout_in      = timeout_in;
  _next_inactive_timeout_at = Thread::get_hrtime() + timeout_in;
}

inline void
NetTimeout::cancel_active_timeout()
{
  _active_timeout_in      = 0;
  _next_active_timeout_at = 0;
}

inline void
NetTimeout::cancel_inactive_timeout()
{
  _inactive_timeout_in      = 0;
  _next_inactive_timeout_at = 0;
}

inline void
NetTimeout::reset_active_timeout()
{
  if (_active_timeout_in == 0) {
    return;
  }

  _next_active_timeout_at = Thread::get_hrtime() + _active_timeout_in;
}

inline void
NetTimeout::reset_inactive_timeout()
{
  if (_inactive_timeout_in == 0) {
    return;
  }

  _next_inactive_timeout_at = Thread::get_hrtime() + _inactive_timeout_in;
}

inline bool
NetTimeout::is_active_timeout_expired(ink_hrtime now)
{
  ink_assert(now > 0);

  if (_active_timeout_in == 0) {
    return false;
  }

  if (0 < _next_active_timeout_at && _next_active_timeout_at < now) {
    Debug("activity_cop", "active timeout cont=%p now=%" PRId64 " timeout_at=%" PRId64 " timeout_in=%" PRId64, this,
          ink_hrtime_to_sec(now), ink_hrtime_to_sec(_next_active_timeout_at), ink_hrtime_to_sec(_active_timeout_in));
    return true;
  }

  return false;
}

inline bool
NetTimeout::is_inactive_timeout_expired(ink_hrtime now)
{
  ink_assert(now > 0);

  if (_inactive_timeout_in == 0) {
    return false;
  }

  if (0 < _next_inactive_timeout_at && _next_inactive_timeout_at < now) {
    Debug("activity_cop", "inactive timeout cont=%p now=%" PRId64 " timeout_at=%" PRId64 " timeout_in=%" PRId64, this,
          ink_hrtime_to_sec(now), ink_hrtime_to_sec(_next_inactive_timeout_at), ink_hrtime_to_sec(_inactive_timeout_in));
    return true;
  }

  return false;
}

/**
  Return how log this was inactive.
 */
inline ink_hrtime
NetTimeout::idle_time(ink_hrtime now)
{
  if (now < _next_inactive_timeout_at) {
    return 0;
  }

  return ink_hrtime_to_sec((now - _next_inactive_timeout_at) + _inactive_timeout_in);
}

inline void
NetTimeout::update_inactivity()
{
  if (_inactive_timeout_in == 0) {
    return;
  }

  _next_inactive_timeout_at = Thread::get_hrtime() + _inactive_timeout_in;
}

//
// ActivityCop
//
template <class T, class List>
inline ActivityCop<T, List>::ActivityCop(Ptr<ProxyMutex> &m, List *l, int f) : Continuation(m.get()), _list(l), _freq(f)
{
  SET_HANDLER((&ActivityCop<T, List>::check_activity));
}

template <class T, class List>
inline void
ActivityCop<T, List>::start()
{
  _event = this_ethread()->schedule_every(this, HRTIME_SECONDS(_freq));
}

template <class T, class List>
inline void
ActivityCop<T, List>::stop()
{
  _event->cancel();
}

template <class T, class List>
inline int
ActivityCop<T, List>::check_activity(int /* event */, Event *e)
{
  ink_hrtime now = Thread::get_hrtime();

  // Traverse list & check inactivity or activity
  T *t = _list->head;
  while (t) {
    T *next = _list->next(t);
    if (t->mutex == nullptr) {
      t = next;
      continue;
    }

    MUTEX_TRY_LOCK(lock, t->mutex, this_ethread());
    if (!lock.is_locked()) {
      t = next;
      continue;
    }

    if (t->is_inactive_timeout_expired(now)) {
      t->handleEvent(VC_EVENT_INACTIVITY_TIMEOUT, e);
    } else if (t->is_active_timeout_expired(now)) {
      t->handleEvent(VC_EVENT_ACTIVE_TIMEOUT, e);
    }

    t = next;
  }

  return EVENT_DONE;
}
