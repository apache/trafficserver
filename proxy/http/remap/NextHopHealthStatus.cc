/** @file

  Implementation of nexthop consistent hash selections strategies.

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

#include "NextHopSelectionStrategy.h"
#include "HttpSM.h"

/**
 * initialize the host_map
 */
void
NextHopHealthStatus::insert(std::vector<std::shared_ptr<HostRecord>> &hosts)
{
  for (auto h : hosts) {
    for (auto protocol = h->protocols.begin(); protocol != h->protocols.end(); ++protocol) {
      const std::string host_port = h->getHostPort((*protocol)->port);
      host_map.emplace(std::make_pair(host_port, h));
      NH_Debug(NH_DEBUG_TAG, "inserting %s into host_map", host_port.c_str());
    }
  }
}

/**
 * check that hostname is available for use.
 */
bool
NextHopHealthStatus::isNextHopAvailable(TSHttpTxn txn, const char *hostname, const int port, void *ih)
{
  HttpSM *sm    = reinterpret_cast<HttpSM *>(txn);
  int64_t sm_id = sm->sm_id;

  const std::string host_port = HostRecord::makeHostPort(hostname, port);
  auto iter                   = host_map.find(host_port);

  if (iter == host_map.end()) {
    NH_Debug(NH_DEBUG_TAG, "[%" PRId64 "] no host named %s found in host_map", sm_id, host_port.c_str());
    return false;
  }

  std::shared_ptr p = iter->second;
  return p->available.load();
}

/**
 * mark up or down the indicated host
 */
void
NextHopHealthStatus::markNextHop(TSHttpTxn txn, const char *hostname, const int port, const NHCmd status, void *ih,
                                 const time_t now)
{
  time_t _now;
  now == 0 ? _now = time(nullptr) : _now = now;

  HttpSM *sm              = reinterpret_cast<HttpSM *>(txn);
  ParentResult result     = sm->t_state.parent_result;
  int64_t sm_id           = sm->sm_id;
  int64_t fail_threshold  = sm->t_state.txn_conf->parent_fail_threshold;
  int64_t retry_time      = sm->t_state.txn_conf->parent_retry_time;
  uint32_t new_fail_count = 0;

  // make sure we're called back with a result structure for a parent
  // that is being retried.
  if (status == NH_MARK_UP) {
    ink_assert(result.retry == true);
  }
  if (result.result != PARENT_SPECIFIED) {
    return;
  }

  // No failover exists when the result is set through the API.
  if (result.is_api_result()) {
    return;
  }

  const std::string host_port = HostRecord::makeHostPort(hostname, port);
  auto iter                   = host_map.find(host_port);
  if (iter == host_map.end()) {
    NH_Debug(NH_DEBUG_TAG, "[%" PRId64 "] no host named %s found in host_map", sm_id, host_port.c_str());
    return;
  }

  std::shared_ptr h = iter->second;

  switch (status) {
  // Mark the host up.
  case NH_MARK_UP:
    if (!h->available.load()) {
      h->set_available();
      NH_Note("[%" PRId64 "] http parent proxy %s restored", sm_id, hostname);
    }
    break;
  // Mark the host down.
  case NH_MARK_DOWN:
    if (h->failedAt.load() == 0 || result.retry == true) {
      { // lock guard
        std::lock_guard<std::mutex> guard(h->_mutex);
        if (h->failedAt.load() == 0) {
          h->failedAt = _now;
          if (result.retry == false) {
            new_fail_count = h->failCount = 1;
          }
        } else if (result.retry == true) {
          h->failedAt    = _now;
          new_fail_count = h->failCount += 1;
        }
      } // end lock guard
      NH_Note("[%" PRId64 "] NextHop %s marked as down %s", sm_id, (result.retry) ? "retry" : "initially", h->hostname.c_str());
    } else {
      // if the last failure was outside the retry window, set the failcount to 1 and failedAt to now.
      { // lock guard
        std::lock_guard<std::mutex> lock(h->_mutex);
        if ((h->failedAt.load() + retry_time) < static_cast<unsigned>(_now)) {
          new_fail_count = h->failCount = 1;
          h->failedAt                   = _now;
        } else {
          new_fail_count = h->failCount += 1;
        }
      } // end of lock_guard
      NH_Debug(NH_DEBUG_TAG, "[%" PRId64 "] Parent fail count increased to %d for %s", sm_id, new_fail_count, h->hostname.c_str());
    }

    if (new_fail_count >= fail_threshold) {
      h->set_unavailable();
      NH_Note("[%" PRId64 "] Failure threshold met failcount:%d >= threshold:%" PRId64 ", http parent proxy %s marked down", sm_id,
              new_fail_count, fail_threshold, h->hostname.c_str());
      NH_Debug(NH_DEBUG_TAG, "[%" PRId64 "] NextHop %s marked unavailable, h->available=%s", sm_id, h->hostname.c_str(),
               (h->available.load()) ? "true" : "false");
    }
    break;
  }
}
