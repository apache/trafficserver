/** @file

  Implementation of various round robin nexthop selections strategies.

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

#include <mutex>
#include <yaml-cpp/yaml.h>

#include "NextHopRoundRobin.h"

NextHopRoundRobin::~NextHopRoundRobin()
{
  NH_Debug(NH_DEBUG_TAG, "destructor called for strategy named: %s", strategy_name.c_str());
}

void
NextHopRoundRobin::findNextHop(const uint64_t sm_id, ParentResult &result, RequestData &rdata, const uint64_t fail_threshold,
                               const uint64_t retry_time, time_t now)
{
  time_t _now      = now;
  bool firstcall   = true;
  bool parentUp    = false;
  bool parentRetry = false;
  bool wrapped     = result.wrap_around;
  std::vector<bool> wrap_around(groups, false);
  uint32_t cur_hst_index = 0;
  uint32_t cur_grp_index = 0;
  uint32_t hst_size      = host_groups[cur_grp_index].size();
  uint32_t start_group   = 0;
  uint32_t start_host    = 0;
  std::shared_ptr<HostRecord> cur_host;
  HostStatus &pStatus           = HostStatus::instance();
  HttpRequestData *request_info = static_cast<HttpRequestData *>(&rdata);
  HostStatus_t host_stat        = HostStatus_t::HOST_STATUS_UP;

  if (result.line_number != -1 && result.result != PARENT_UNDEFINED) {
    firstcall = false;
  }

  if (firstcall) {
    // distance is the index into the strategies map, this is the equivalent to the old line_number in parent.config.
    result.line_number = distance;
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] first call , cur_grp_index: %d, cur_hst_index: %d, distance: %d", sm_id, cur_grp_index,
             cur_hst_index, distance);
    switch (policy_type) {
    case NH_FIRST_LIVE:
      result.start_parent = cur_hst_index = 0;
      cur_grp_index                       = 0;
      break;
    case NH_RR_STRICT: {
      std::lock_guard<std::mutex> lock(_mutex);
      cur_hst_index = result.start_parent = this->hst_index;
      cur_grp_index                       = 0;
      this->hst_index                     = (this->hst_index + 1) % hst_size;
    } break;
    case NH_RR_IP:
      cur_grp_index = 0;
      if (rdata.get_client_ip() != nullptr) {
        cur_hst_index = result.start_parent = ntohl(ats_ip_hash(rdata.get_client_ip())) % hst_size;
      } else {
        cur_hst_index = this->hst_index;
      }
      break;
    case NH_RR_LATCHED:
      cur_grp_index = 0;
      cur_hst_index = result.start_parent = latched_index;
      break;
    default:
      ink_assert(0);
      break;
    }
    cur_host = host_groups[cur_grp_index][cur_hst_index];
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] first call, cur_grp_index: %d, cur_hst_index: %d", sm_id, cur_grp_index, cur_hst_index);
  } else {
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] next call, cur_grp_index: %d, cur_hst_index: %d, distance: %d", sm_id, cur_grp_index,
             cur_hst_index, distance);
    // Move to next parent due to failure
    latched_index = cur_hst_index = (result.last_parent + 1) % hst_size;
    cur_host                      = host_groups[cur_grp_index][cur_hst_index];

    // Check to see if we have wrapped around
    if (static_cast<unsigned int>(cur_hst_index) == result.start_parent) {
      // We've wrapped around so bypass if we can
      if (go_direct == true) {
        result.result = PARENT_DIRECT;
      } else {
        result.result = PARENT_FAIL;
      }
      result.hostname    = nullptr;
      result.port        = 0;
      result.wrap_around = true;
      return;
    }
  }
  start_group = cur_grp_index;
  start_host  = cur_hst_index;

  // Verify that the 'cur_hst' is available or retryable, if not loop through the array of parents seeing if any are up or
  // should be retried
  do {
    HostStatRec *hst = pStatus.getHostStatus(cur_host->hostname.c_str());
    host_stat        = (hst) ? hst->status : HostStatus_t::HOST_STATUS_UP;
    // if the config ignore_self_detect is set to true and the host is down due to SELF_DETECT reason
    // ignore the down status and mark it as avaialble
    if (ignore_self_detect && (hst && hst->status == HOST_STATUS_DOWN)) {
      if (hst->reasons == Reason::SELF_DETECT) {
        host_stat = HOST_STATUS_UP;
      }
    }

    NH_Debug(NH_DEBUG_TAG,
             "[%" PRIu64 "] Selected a parent, %s,  failCount (faileAt: %d failCount: %d), FailThreshold: %" PRIu64
             ", request_info->xact_start: %ld",
             sm_id, cur_host->hostname.c_str(), (unsigned)cur_host->failedAt, cur_host->failCount, fail_threshold,
             request_info->xact_start);
    // check if 'cur_host' is available, mark it up if it is.
    if ((cur_host->failedAt == 0) || (cur_host->failCount < fail_threshold)) {
      if (host_stat == HOST_STATUS_UP) {
        NH_Debug(NH_DEBUG_TAG,
                 "[%" PRIu64
                 "] Selecting a parent, %s,  due to little failCount (faileAt: %d failCount: %d), FailThreshold: %" PRIu64,
                 sm_id, cur_host->hostname.c_str(), (unsigned)cur_host->failedAt, cur_host->failCount, fail_threshold);
        parentUp = true;
      }
    } else { // if not available, check to see if it can be retried.  If so, set the retry flag and temporairly mark it as
             // available.
      _now == 0 ? _now = time(nullptr) : _now = now;
      if (((result.wrap_around) || (cur_host->failedAt + retry_time) < static_cast<unsigned>(_now)) &&
          host_stat == HOST_STATUS_UP) {
        // Reuse the parent
        parentUp    = true;
        parentRetry = true;
        NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "]  NextHop marked for retry %s:%d", sm_id, cur_host->hostname.c_str(),
                 host_groups[cur_grp_index][cur_hst_index]->getPort(scheme));
      } else { // not retryable or available.
        parentUp = false;
      }
    }
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] parentUp: %s, hostname: %s, host status: %s", sm_id, parentUp ? "true" : "false",
             cur_host->hostname.c_str(), HostStatusNames[host_stat]);

    // The selected host is available or retryable, return the search result.
    if (parentUp == true && host_stat != HOST_STATUS_DOWN) {
      NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] status for %s: %s", sm_id, cur_host->hostname.c_str(), HostStatusNames[host_stat]);
      result.result      = PARENT_SPECIFIED;
      result.hostname    = cur_host->hostname.c_str();
      result.port        = cur_host->getPort(scheme);
      result.last_parent = cur_hst_index;
      result.last_group  = cur_grp_index;
      result.retry       = parentRetry;
      ink_assert(result.hostname != nullptr);
      ink_assert(result.port != 0);
      NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] Chosen parent = %s.%d", sm_id, result.hostname, result.port);
      return;
    }

    // only one host group is available, find another host if we have not wrapped.
    if (groups == 1) {
      latched_index = cur_hst_index = (cur_hst_index + 1) % hst_size;
      if (start_host == cur_hst_index) {
        wrap_around[cur_grp_index] = wrapped = result.wrap_around = true;
      }
    } else {                                // search the fail over groups.
      if (ring_mode == NH_ALTERNATE_RING) { // use alternating ring mode.
        cur_grp_index = (cur_grp_index + 1) % groups;
        hst_size      = host_groups[cur_grp_index].size();
        if (cur_grp_index == start_group) {
          latched_index = cur_hst_index = (cur_hst_index + 1) % hst_size;
          if (cur_hst_index == start_host) {
            wrapped = wrap_around[cur_grp_index] = result.wrap_around = true;
          }
        }
      } else { // use the exhaust ring mode.
        latched_index = cur_hst_index = (cur_hst_index + 1) % hst_size;
        if (cur_hst_index == start_host) {
          wrap_around[cur_grp_index] = true;
          cur_grp_index              = (cur_grp_index + 1) % groups;
          if (cur_grp_index == start_group) {
            wrapped = wrap_around[cur_grp_index] = result.wrap_around = true;
          } else {
            start_host = cur_hst_index = 0;
          }
        }
      }
    }
    cur_host = host_groups[cur_grp_index][cur_hst_index];
    NH_Debug(
      NH_DEBUG_TAG,
      "[%" PRIu64 "] host: %s, groups: %d, cur_grp_index: %d, cur_hst_index: %d, wrapped: %s, start_group: %d, start_host: %d",
      sm_id, cur_host->hostname.c_str(), groups, cur_grp_index, cur_hst_index, wrapped ? "true" : "false", start_group, start_host);
  } while (!wrapped);

  if (go_direct == true) {
    result.result = PARENT_DIRECT;
  } else {
    result.result = PARENT_FAIL;
  }

  result.hostname = nullptr;
  result.port     = 0;
}
