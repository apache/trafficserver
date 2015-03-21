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
 *  CongestionStats.cc - Implementation of Congestion Control
 *
 *
 ****************************************************************************/

#include "CongestionStats.h"

void
register_congest_stats()
{
#define CONGEST_CLEAR_DYN_STAT(x)          \
  do {                                     \
    RecSetRawStatSum(congest_rsb, x, 0);   \
    RecSetRawStatCount(congest_rsb, x, 0); \
  } while (0);

  congest_rsb = RecAllocateRawStatBlock((int)congest_num_stats);
  RecRegisterRawStat(congest_rsb, RECT_PROCESS, "proxy.process.congestion.congested_on_conn_failures", RECD_INT,
                     RECP_NON_PERSISTENT, (int)congested_on_F_stat, RecRawStatSyncSum);
  CONGEST_CLEAR_DYN_STAT(congested_on_F_stat);

  RecRegisterRawStat(congest_rsb, RECT_PROCESS, "proxy.process.congestion.congested_on_max_connection", RECD_INT,
                     RECP_NON_PERSISTENT, (int)congested_on_M_stat, RecRawStatSyncSum);
  CONGEST_CLEAR_DYN_STAT(congested_on_M_stat);
}
