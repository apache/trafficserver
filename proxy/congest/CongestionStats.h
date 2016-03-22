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

/*****************************************************************************
 *
 *  CongestionStats.h - Implementation of Congestion Control
 *
 *
 ****************************************************************************/
#ifndef CONGESTION_STATS_H_
#define CONGESTION_STATS_H_

void register_congest_stats();
#include "P_RecProcess.h"
extern RecRawStatBlock *congest_rsb;

/* Instead of enumerating the stats in DynamicStats.h, each module needs
   to enumerate its stats separately and register them with librecords
   */
enum {
  congested_on_F_stat,
  congested_on_M_stat,
  congest_num_stats,
};
#define CONGEST_SUM_GLOBAL_DYN_STAT(_x, _y) RecIncrGlobalRawStatSum(congest_rsb, (int)_x, _y)
#define CONGEST_INCREMENT_DYN_STAT(_x) RecIncrRawStat(congest_rsb, mutex->thread_holding, (int)_x, 1)

#endif /* CONGESTION_STATS_H_ */
