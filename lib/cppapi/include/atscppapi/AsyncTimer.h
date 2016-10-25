/**
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

/**
 * @file AsyncTimer.h
 */

#pragma once
#ifndef ATSCPPAPI_ASYNCTIMER_H_
#define ATSCPPAPI_ASYNCTIMER_H_

#include <string>
#include <memory>
#include <atscppapi/Async.h>
#include <atscppapi/Request.h>
#include <atscppapi/Response.h>

namespace atscppapi
{
// forward declarations
struct AsyncTimerState;

/**
 * @brief This class provides an implementation of AsyncProvider that
 * acts as a timer. It sends events at the set frequency. Calling the
 * destructor will stop the events. A one-off timer just sends one
 * event. Calling the destructor before this event will cancel the timer.
 *
 * For either type, user must delete the timer.
 *
 * See example async_timer for sample usage.
 */
class AsyncTimer : public AsyncProvider
{
public:
  enum Type {
    TYPE_ONE_OFF = 0,
    TYPE_PERIODIC,
  };

  /**
   * Constructor.
   *
   * @param type A one-off timer fires only once and a periodic timer fires periodically.
   * @param period_in_ms The receiver will receive an event every this many milliseconds.
   * @param initial_period_in_ms The first event will arrive after this many milliseconds. Subsequent
   *                             events will have "regular" cadence. This is useful if the timer is
   *                             set for a long period of time (1hr etc.), but an initial event is
   *                             required. Value of 0 (default) indicates no initial event is desired.
   */
  AsyncTimer(Type type, int period_in_ms, int initial_period_in_ms = 0);

  virtual ~AsyncTimer();

  /**
   * Starts the timer.
   */
  void run();

  void cancel();

private:
  AsyncTimerState *state_;
};

} /* atscppapi */

#endif /* ATSCPPAPI_ASYNCTIMER_H_ */
