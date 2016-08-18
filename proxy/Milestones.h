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

#if !defined(_Milestones_h_)
#define _Milestones_h_

#include "ts/ink_platform.h"
#include "ts/ink_hrtime.h"
#include "ts/apidefs.h"
#include "../lib/ts/ink_hrtime.h"

/////////////////////////////////////////////////////////////
//
// class TransactionMilestones
//
/////////////////////////////////////////////////////////////
class TransactionMilestones
{
public:
  TransactionMilestones() { ink_zero(milestones); }
  ink_hrtime &operator[](TSMilestonesType ms) { return milestones[ms]; }
  ink_hrtime operator[](TSMilestonesType ms) const { return milestones[ms]; }
  /**
   * Takes two milestones and returns the difference.
   * @param start The start time
   * @param end The end time
   * @return The difference time in milliseconds
   */
  int64_t
  difference_msec(TSMilestonesType ms_start, TSMilestonesType ms_end) const
  {
    if (milestones[ms_end] == 0) {
      return -1;
    }
    return ink_hrtime_to_msec(milestones[ms_end] - milestones[ms_start]);
  }

  /**
   * Takes two milestones and returns the difference.
   * @param start The start time
   * @param end The end time
   * @return A double that is the difference time in seconds
   */
  double
  difference_sec(TSMilestonesType ms_start, TSMilestonesType ms_end) const
  {
    return (double)difference_msec(ms_start, ms_end) / 1000.0;
  }

  ink_hrtime
  elapsed(TSMilestonesType ms_start, TSMilestonesType ms_end) const
  {
    return milestones[ms_end] - milestones[ms_start];
  }

private:
  ink_hrtime milestones[TS_MILESTONE_LAST_ENTRY];
};

#endif /* _Milestones_h_ */
