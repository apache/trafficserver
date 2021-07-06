/** @file

  Milestones

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

#include "ts/apidefs.h"

#include "tscore/ink_platform.h"
#include "tscore/ink_hrtime.h"

#include "I_EventSystem.h"

template <class T, size_t entries> class Milestones
{
public:
  ink_hrtime &
  operator[](T ms)
  {
    return this->_milestones[static_cast<size_t>(ms)];
  }
  ink_hrtime
  operator[](T ms) const
  {
    return this->_milestones[static_cast<size_t>(ms)];
  }

  /**
   * Mark given milestone with timestamp if it's not marked yet
   * @param ms The milestone to mark
   * @return N/A
   */
  void
  mark(T ms)
  {
    if (this->_milestones[static_cast<size_t>(ms)] == 0) {
      this->_milestones[static_cast<size_t>(ms)] = Thread::get_hrtime();
    }
  }

  /**
   * Takes two milestones and returns the difference.
   * @param start The start time
   * @param end The end time
   * @return The difference time in milliseconds
   */
  int64_t
  difference_msec(T ms_start, T ms_end) const
  {
    if (this->_milestones[static_cast<size_t>(ms_end)] == 0) {
      return -1;
    }
    return ink_hrtime_to_msec(this->_milestones[static_cast<size_t>(ms_end)] - this->_milestones[static_cast<size_t>(ms_start)]);
  }

  /**
   * Takes two milestones and returns the difference.
   * @param start The start time
   * @param end The end time
   * @return A double that is the difference time in seconds
   */
  double
  difference_sec(T ms_start, T ms_end) const
  {
    return static_cast<double>(difference_msec(ms_start, ms_end) / 1000.0);
  }

  /**
   * Takes two milestones and returns the difference.
   * @param start The start time
   * @param end The end time
   * @return The difference time in high-resolution time
   */
  ink_hrtime
  elapsed(T ms_start, T ms_end) const
  {
    return this->_milestones[static_cast<size_t>(ms_end)] - this->_milestones[static_cast<size_t>(ms_start)];
  }

private:
  std::array<ink_hrtime, entries> _milestones = {{0}};
};

// For compatibility with HttpSM.h and HttpTransact.h
using TransactionMilestones = Milestones<TSMilestonesType, TS_MILESTONE_LAST_ENTRY>;
