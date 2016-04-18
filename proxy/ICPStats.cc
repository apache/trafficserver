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

/****************************************************************************

  ICPStats.cc


****************************************************************************/

#include "Main.h"
#include "P_EventSystem.h"
#include "P_Cache.h"
#include "P_RecProcess.h"
#include "ICP.h"

struct RecRawStatBlock *icp_rsb;

void
ICPProcessor::InitICPStatCallbacks()
{
  icp_rsb = RecAllocateRawStatBlock((int)icp_stat_count);

  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.config_mgmt_callouts", RECD_INT, RECP_PERSISTENT,
                     (int)config_mgmt_callouts_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.reconfig_polls", RECD_INT, RECP_PERSISTENT, (int)reconfig_polls_stat,
                     RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.reconfig_events", RECD_INT, RECP_PERSISTENT,
                     (int)reconfig_events_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.invalid_poll_data", RECD_INT, RECP_PERSISTENT,
                     (int)invalid_poll_data_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.no_data_read", RECD_INT, RECP_PERSISTENT, (int)no_data_read_stat,
                     RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.short_read", RECD_INT, RECP_PERSISTENT, (int)short_read_stat,
                     RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.invalid_sender", RECD_INT, RECP_PERSISTENT, (int)invalid_sender_stat,
                     RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.read_not_v2_icp", RECD_INT, RECP_PERSISTENT,
                     (int)read_not_v2_icp_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.icp_remote_query_requests", RECD_INT, RECP_PERSISTENT,
                     (int)icp_remote_query_requests_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.icp_remote_responses", RECD_INT, RECP_PERSISTENT,
                     (int)icp_remote_responses_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.cache_lookup_success", RECD_INT, RECP_PERSISTENT,
                     (int)icp_cache_lookup_success_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.cache_lookup_fail", RECD_INT, RECP_PERSISTENT,
                     (int)icp_cache_lookup_fail_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.query_response_write", RECD_INT, RECP_PERSISTENT,
                     (int)query_response_write_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.query_response_partial_write", RECD_INT, RECP_PERSISTENT,
                     (int)query_response_partial_write_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.no_icp_request_for_response", RECD_INT, RECP_PERSISTENT,
                     (int)no_icp_request_for_response_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.icp_response_request_nolock", RECD_INT, RECP_PERSISTENT,
                     (int)icp_response_request_nolock_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.icp_start_icpoff", RECD_INT, RECP_PERSISTENT,
                     (int)icp_start_icpoff_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.send_query_partial_write", RECD_INT, RECP_PERSISTENT,
                     (int)send_query_partial_write_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.icp_queries_no_expected_replies", RECD_INT, RECP_PERSISTENT,
                     (int)icp_queries_no_expected_replies_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.icp_query_hits", RECD_INT, RECP_PERSISTENT, (int)icp_query_hits_stat,
                     RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.icp_query_misses", RECD_INT, RECP_PERSISTENT,
                     (int)icp_query_misses_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.invalid_icp_query_response", RECD_INT, RECP_PERSISTENT,
                     (int)invalid_icp_query_response_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.icp_query_requests", RECD_INT, RECP_PERSISTENT,
                     (int)icp_query_requests_stat, RecRawStatSyncCount);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.total_icp_response_time", RECD_FLOAT, RECP_PERSISTENT,
                     (int)total_icp_response_time_stat, RecRawStatSyncMHrTimeAvg);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.total_udp_send_queries", RECD_INT, RECP_PERSISTENT,
                     (int)total_udp_send_queries_stat, RecRawStatSyncSum);
  RecRegisterRawStat(icp_rsb, RECT_PROCESS, "proxy.process.icp.total_icp_request_time", RECD_FLOAT, RECP_PERSISTENT,
                     (int)total_icp_request_time_stat, RecRawStatSyncMHrTimeAvg);
}

// End of ICPStats.cc
