/** @file

  Calculate some derivative metrics (for convenience).

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
#include <vector>

#include "DerivativeMetrics.h"

// ToDo: It's a little bizarre that we include this here, but it's the only way to get to RecSetRecord(). We should
// move that elsewhere... But other places in our core does the same thing.
#include "records/P_RecCore.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This currently only supports one type of derivative metrics: Sums() of other, existing metrics. It's ok to add
// additional metrics here, and we prefer to call them proxy.process (since, hopefully in the future, traffic_manager dies).
//
static const std::vector<DerivativeSum> sum_metrics = {
  // Total bytes of client request body + headers
  {"proxy.process.http.user_agent_total_request_bytes",
   RECD_INT,
   {"proxy.process.http.user_agent_request_document_total_size", "proxy.process.http.user_agent_request_header_total_size"}},
  // Total bytes of client response body + headers
  {"proxy.process.http.user_agent_total_response_bytes",
   RECD_INT,
   {"proxy.process.http.user_agent_response_document_total_size", "proxy.process.http.user_agent_response_header_total_size"}},
  // Total bytes of origin server request body + headers
  {"proxy.process.http.origin_server_total_request_bytes",
   RECD_INT,
   {"proxy.process.http.origin_server_request_document_total_size", "proxy.process.http.origin_server_request_header_total_size"}},
  // Total bytes of origin server response body + headers
  {"proxy.process.http.origin_server_total_response_bytes",
   RECD_INT,
   {"proxy.process.http.origin_server_response_document_total_size",
    "proxy.process.http.origin_server_response_header_total_size"}},
  // Total byates of client request and response (total traffic to and from clients)
  {"proxy.process.user_agent_total_bytes",
   RECD_INT,
   {"proxy.process.http.user_agent_total_request_bytes", "proxy.process.http.user_agent_total_response_bytes"}},
  // Total bytes of origin/parent request and response
  {"proxy.process.origin_server_total_bytes",
   RECD_INT,
   {"proxy.process.http.origin_server_total_request_bytes", "proxy.process.http.origin_server_total_response_bytes",
    "proxy.process.http.parent_proxy_request_total_bytes", "proxy.process.http.parent_proxy_response_total_bytes"}},
  // Total requests which are cache hits
  {"proxy.process.cache_total_hits",
   RECD_COUNTER,
   {"proxy.process.http.cache_hit_fresh", "proxy.process.http.cache_hit_revalidated", "proxy.process.http.cache_hit_ims",
    "proxy.process.http.cache_hit_stale_served"}},
  // Total requests which are cache misses
  {"proxy.process.cache_total_misses",
   RECD_COUNTER,
   {"proxy.process.http.cache_miss_cold", "proxy.process.http.cache_miss_changed", "proxy.process.http.cache_miss_client_no_cache",
    "proxy.process.http.cache_miss_ims", "proxy.process.http.cache_miss_client_not_cacheable"}},
  // Total requests, both hits and misses (this is slightly superflous, but assures correct percentage calculations)
  {"proxy.process.cache_total_requests", RECD_COUNTER, {"proxy.process.cache_total_hits", "proxy.process.cache_total_misses"}},
  // Total cache requests bytes which are cache hits
  {"proxy.process.cache_total_hits_bytes",
   RECD_INT,
   {"proxy.process.http.tcp_hit_user_agent_bytes_stat", "proxy.process.http.tcp_refresh_hit_user_agent_bytes_stat",
    "proxy.process.http.tcp_ims_hit_user_agent_bytes_stat"}},
  // Total cache requests bytes which are cache misses
  {"proxy.process.cache_total_misses_bytes",
   RECD_INT,
   {"proxy.process.http.tcp_miss_user_agent_bytes_stat", "proxy.process.http.tcp_expired_miss_user_agent_bytes_stat",
    "proxy.process.http.tcp_refresh_miss_user_agent_bytes_stat", "proxy.process.http.tcp_ims_miss_user_agent_bytes_stat"}},
  // Total request bytes, both hits and misses
  {"proxy.process.cache_total_bytes", RECD_INT, {"proxy.process.cache_total_hits_bytes", "proxy.process.cache_total_misses_bytes"}},
  // Total of all server connections (sum of origins and parent connections)
  {"proxy.process.current_server_connections",
   RECD_INT,
   {"proxy.process.http.current_server_connections", "proxy.process.http.current_parent_proxy_connections"}}};

// The constructor is responsible for registering the new metrics. ToDo: At some point we could
// in theory expand this to support some sort of configuration, and then replace the hardcoded metrics
// here with parameters to an Add() method.
DerivativeMetrics::DerivativeMetrics()
{
  // Add all the sum derived metrics to LibRecords
  for (auto &&[derived_metric, type, metric_parts] : sum_metrics) {
    (void)metric_parts;

    switch (type) {
    case RECD_INT:
      RecRegisterStatInt(RECT_PROCESS, derived_metric, static_cast<RecInt>(0), RECP_NON_PERSISTENT);
      break;
    case RECD_COUNTER:
      RecRegisterStatCounter(RECT_PROCESS, derived_metric, static_cast<RecCounter>(0), RECP_NON_PERSISTENT);
      break;
    default:
      ink_release_assert(!"Unsupported metric type");
      break;
    }
  }
}

// Updates all the derived metrics
void
DerivativeMetrics::Update()
{
  int error = REC_ERR_FAIL;
  RecData sum;
  RecInt int_val;
  RecCounter counter_val;

  for (auto &&[derived_metric, type, metric_parts] : sum_metrics) {
    ink_zero(sum);
    for (auto &&metric : metric_parts) {
      switch (type) {
      case RECD_INT:
        error = RecGetRecordInt(metric, &int_val);
        sum.rec_int += int_val;
        break;
      case RECD_COUNTER:
        error = RecGetRecordCounter(metric, &counter_val);
        sum.rec_counter += counter_val;
        break;
      default:
        ink_release_assert(!"Unsupported metric type");
        break;
      }
      if (error) { // No point in continuing here, since the metric is failing for odd reasons
        break;
      }
    }

    if (!error) {
      RecSetRecord(RECT_NULL, derived_metric, type, &sum, nullptr, REC_SOURCE_EXPLICIT);
    }
  }
}
