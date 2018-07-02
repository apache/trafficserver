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

#include <ctime>
#include <vector>
#include <map>
#include <sys/time.h>

#if defined(solaris) && !defined(timersub)
#define timersub(a, b, result)                       \
  do {                                               \
    (result)->tv_sec  = (a)->tv_sec - (b)->tv_sec;   \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
    if ((result)->tv_usec < 0) {                     \
      --(result)->tv_sec;                            \
      (result)->tv_usec += 1000000;                  \
    }                                                \
  } while (0)
#endif

#include <string>
#include <pthread.h>

#include "ComponentBase.h"

using namespace std;

typedef std::vector<std::pair<double, double>> FailureToSuccess;
typedef std::map<std::string, class FailureInfo *> FailureData;

static const int WINDOW_SIZE    = 200;
static const int TOTAL_DURATION = 2000;

class FailureInfo : private EsiLib::ComponentBase
{
public:
  FailureInfo(const char *debug_tag, ComponentBase::Debug debug_func, ComponentBase::Error error_func)
    : ComponentBase(debug_tag, debug_func, error_func), _windowsPassed(0), _avgOverWindow(0), _requestMade(true)
  {
    _totalSlots   = TOTAL_DURATION / WINDOW_SIZE;
    _windowMarker = 0;
    for (size_t i = 0; i < _totalSlots; i++) {
      _statistics.push_back(make_pair(0, 0));
    }
    _debugLog(_debug_tag, "FailureInfo Ctor:inserting URL object into the statistics map [FailureInfo object]%p", this);
  };

  ~FailureInfo() override {}
  /* Fills the statistics vector depending
   * upon the position of the window marker
   */
  void registerSuccFail(bool isSuccess);

  /*
   * Decides if an attempt shud be made
   * for the attempt tag or except tag
   * depending upon the statistics
   */
  bool isAttemptReq();

private:
  /*
   * Keeps track of failures of attempt
   * vs success
   */
  FailureToSuccess _statistics;

  /* Slot on which to register success/failures
   * Changes as soon as time has passed windowSize
   */
  size_t _windowMarker;

  /* Number of slots to be filled over time */
  size_t _totalSlots;

  /* Start time for the window slots */
  struct timeval _start;

  /* Keep track of the number of windows filled prev*/
  size_t _windowsPassed;

  /*Used as a deciding factor between attempt/except
   * incase prob is complete truth
   */
  double _avgOverWindow;

public:
  /*Was a reqeust made*/
  bool _requestMade;
};
