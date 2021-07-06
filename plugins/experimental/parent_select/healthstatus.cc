/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "strategy.h"
#include "consistenthash.h"
#include "util.h"

#include <cinttypes>
#include <string>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <cstring>

#include <sys/stat.h>
#include <dirent.h>

#include <yaml-cpp/yaml.h>

#include "tscore/HashSip.h"
#include "tscore/ConsistentHash.h"
#include "tscore/ink_assert.h"
#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/parentselectdefs.h"

void
PLNextHopHealthStatus::insert(std::vector<std::shared_ptr<PLHostRecord>> &hosts)
{
  for (auto h : hosts) {
    for (auto protocol = h->protocols.begin(); protocol != h->protocols.end(); ++protocol) {
      const std::string host_port = h->getHostPort((*protocol)->port);
      host_map.emplace(std::make_pair(host_port, h));
      PL_NH_Debug(PL_NH_DEBUG_TAG, "inserting %s into host_map", host_port.c_str());
    }
  }
}

void
PLNextHopHealthStatus::mark(TSHttpTxn txnp, PLStatusTxn *state, const char *hostname, const size_t hostname_len,
                            const in_port_t port, const PLNHCmd status, const time_t now)
{
  const time_t _now = now == 0 ? time(nullptr) : now;

  const int64_t sm_id = TSHttpTxnIdGet(txnp);

  int64_t fail_threshold; //  = sm->t_state.txn_conf->parent_fail_threshold;
  if (TSHttpTxnConfigIntGet(txnp, TS_CONFIG_HTTP_PARENT_PROXY_FAIL_THRESHOLD, &fail_threshold) != TS_SUCCESS) {
    PL_NH_Error("mark failed to get parent_fail_threshold, cannot mark next hop");
    return;
  }

  int64_t retry_time; //      = sm->t_state.txn_conf->parent_retry_time;
  if (TSHttpTxnConfigIntGet(txnp, TS_CONFIG_HTTP_PARENT_PROXY_RETRY_TIME, &retry_time) != TS_SUCCESS) {
    PL_NH_Error("mark failed to get parent_retry_time, cannot mark next hop");
    return;
  }

  uint32_t new_fail_count = 0;

  // make sure we're called back with a result structure for a parent
  // that is being retried.
  if (status == PL_NH_MARK_UP) {
    ink_assert(state->retry == true);
  }
  if (state->result != PL_NH_PARENT_SPECIFIED) {
    return;
  }

  const std::string host_port = PLHostRecord::makeHostPort(std::string_view(hostname, hostname_len), port);
  auto iter                   = host_map.find(host_port);
  if (iter == host_map.end()) {
    PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRId64 "] no host named %s found in host_map", sm_id, host_port.c_str());
    return;
  }

  std::shared_ptr h = iter->second;

  switch (status) {
  // Mark the host up.
  case PL_NH_MARK_UP:
    if (!h->available) {
      h->set_available();
      PL_NH_Note("[%" PRId64 "] http parent proxy %s restored", sm_id, hostname);
    }
    break;
  // Mark the host down.
  case PL_NH_MARK_DOWN:
    if (h->failedAt == 0 || state->retry == true) {
      { // lock guard
        std::lock_guard<std::mutex> guard(h->_mutex);
        if (h->failedAt == 0) {
          h->failedAt = _now;
          if (state->retry == false) {
            new_fail_count = h->failCount = 1;
          }
        } else if (state->retry == true) {
          h->failedAt = _now;
        }
      } // end lock guard
      PL_NH_Note("[%" PRId64 "] NextHop %s marked as down %s", sm_id, (state->retry) ? "retry" : "initially", h->hostname.c_str());
    } else {
      int old_count = 0;
      // if the last failure was outside the retry window, set the failcount to 1 and failedAt to now.
      { // lock guard
        std::lock_guard<std::mutex> lock(h->_mutex);
        if ((h->failedAt + retry_time) < static_cast<unsigned>(_now)) {
          h->failCount = 1;
          h->failedAt  = _now;
        } else {
          old_count = h->failCount = 1;
        }
        new_fail_count = old_count + 1;
      } // end of lock_guard
      PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRId64 "] Parent fail count increased to %d for %s", sm_id, new_fail_count,
                  h->hostname.c_str());
    }

    if (new_fail_count >= fail_threshold) {
      h->set_unavailable();
      PL_NH_Note("[%" PRId64 "] Failure threshold met failcount:%d >= threshold:%" PRId64 ", http parent proxy %s marked down",
                 sm_id, new_fail_count, fail_threshold, h->hostname.c_str());
      PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRId64 "] NextHop %s marked unavailable, h->available=%s", sm_id, h->hostname.c_str(),
                  (h->available) ? "true" : "false");
    }
    break;
  }
}
