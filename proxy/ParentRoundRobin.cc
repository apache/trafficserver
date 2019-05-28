/** @file

  Implementation of Parent Proxy routing

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
#include "HostStatus.h"
#include "ParentRoundRobin.h"

ParentRoundRobin::ParentRoundRobin(ParentRecord *parent_record, ParentRR_t _round_robin_type)
{
  round_robin_type = _round_robin_type;
  latched_parent   = 0;
  parents          = parent_record->parents;
  num_parents      = parent_record->num_parents;

  if (is_debug_tag_set("parent_select")) {
    switch (round_robin_type) {
    case P_NO_ROUND_ROBIN:
      Debug("parent_select", "Using a round robin parent selection strategy of type P_NO_ROUND_ROBIN.");
      break;
    case P_STRICT_ROUND_ROBIN:
      Debug("parent_select", "Using a round robin parent selection strategy of type P_STRICT_ROUND_ROBIN.");
      break;
    case P_HASH_ROUND_ROBIN:
      Debug("parent_select", "Using a round robin parent selection strategy of type P_HASH_ROUND_ROBIN.");
      break;
    case P_LATCHED_ROUND_ROBIN:
      Debug("parent_select", "Using a round robin parent selection strategy of type P_LATCHED_ROUND_ROBIN.");
      break;
    default:
      // should never see this, there is a problem if you do.
      Debug("parent_select", "Using a round robin parent selection strategy of type UNKNOWN TYPE.");
      break;
    }
  }
}

ParentRoundRobin::~ParentRoundRobin() {}

void
ParentRoundRobin::selectParent(bool first_call, ParentResult *result, RequestData *rdata, unsigned int fail_threshold,
                               unsigned int retry_time)
{
  Debug("parent_select", "In ParentRoundRobin::selectParent(): Using a round robin parent selection strategy.");
  int cur_index          = 0;
  bool parentUp          = false;
  bool parentRetry       = false;
  HostStatus &pStatus    = HostStatus::instance();
  HostStatus_t host_stat = HostStatus_t::HOST_STATUS_UP;

  HttpRequestData *request_info = static_cast<HttpRequestData *>(rdata);

  ink_assert(num_parents > 0 || result->rec->go_direct == true);

  if (first_call) {
    if (parents == nullptr) {
      // We should only get into this state if
      //   if we are supposed to go direct
      ink_assert(result->rec->go_direct == true);
      // Could not find a parent
      if (result->rec->go_direct == true && result->rec->parent_is_proxy == true) {
        result->result = PARENT_DIRECT;
      } else {
        result->result = PARENT_FAIL;
      }

      result->hostname = nullptr;
      result->port     = 0;
      return;
    } else {
      switch (round_robin_type) {
      case P_HASH_ROUND_ROBIN:
        // INKqa12817 - make sure to convert to host byte order
        // Why was it important to do host order here?  And does this have any
        // impact with the transition to IPv6?  The IPv4 functionality is
        // preserved for now anyway as ats_ip_hash returns the 32-bit address in
        // that case.
        if (rdata->get_client_ip() != nullptr) {
          cur_index = result->start_parent = ntohl(ats_ip_hash(rdata->get_client_ip())) % num_parents;
        } else {
          cur_index = 0;
        }
        break;
      case P_STRICT_ROUND_ROBIN:
        cur_index = ink_atomic_increment(reinterpret_cast<int32_t *>(&result->rec->rr_next), 1);
        cur_index = result->start_parent = cur_index % num_parents;
        break;
      case P_NO_ROUND_ROBIN:
        cur_index = result->start_parent = 0;
        break;
      case P_LATCHED_ROUND_ROBIN:
        cur_index = result->start_parent = latched_parent;
        break;
      default:
        ink_release_assert(0);
      }
    }
  } else {
    // Move to next parent due to failure
    latched_parent = cur_index = (result->last_parent + 1) % num_parents;

    // Check to see if we have wrapped around
    if (static_cast<unsigned int>(cur_index) == result->start_parent) {
      // We've wrapped around so bypass if we can
      if (result->rec->go_direct == true) {
        // Could not find a parent
        if (result->rec->parent_is_proxy == true) {
          result->result = PARENT_DIRECT;
        } else {
          result->result = PARENT_FAIL;
        }
        result->hostname = nullptr;
        result->port     = 0;
        return;
      }
    }
  }

  // Loop through the array of parent seeing if any are up or
  //   should be retried
  do {
    HostStatRec *hst = pStatus.getHostStatus(parents[cur_index].hostname);
    host_stat        = (hst) ? hst->status : HostStatus_t::HOST_STATUS_UP;
    // if the config ignore_self_detect is set to true and the host is down due to SELF_DETECT reason
    // ignore the down status and mark it as avaialble
    if (result->rec->ignore_self_detect && (hst && hst->status == HOST_STATUS_DOWN)) {
      if (hst->reasons == Reason::SELF_DETECT) {
        host_stat = HOST_STATUS_UP;
      }
    }
    Debug("parent_select", "cur_index: %d, result->start_parent: %d", cur_index, result->start_parent);
    // DNS ParentOnly inhibits bypassing the parent so always return that t
    if ((parents[cur_index].failedAt == 0) || (parents[cur_index].failCount < static_cast<int>(fail_threshold))) {
      if (host_stat == HOST_STATUS_UP) {
        Debug("parent_select", "FailThreshold = %d", fail_threshold);
        Debug("parent_select", "Selecting a parent due to little failCount (faileAt: %u failCount: %d)",
              (unsigned)parents[cur_index].failedAt, parents[cur_index].failCount);
        parentUp = true;
      }
    } else {
      if ((result->wrap_around) ||
          ((parents[cur_index].failedAt + retry_time) < request_info->xact_start && host_stat == HOST_STATUS_UP)) {
        Debug("parent_select", "Parent[%d].failedAt = %u, retry = %u,xact_start = %" PRId64 " but wrap = %d", cur_index,
              (unsigned)parents[cur_index].failedAt, retry_time, (int64_t)request_info->xact_start, result->wrap_around);
        // Reuse the parent
        parentUp    = true;
        parentRetry = true;
        Debug("parent_select", "Parent marked for retry %s:%d", parents[cur_index].hostname, parents[cur_index].port);
      } else {
        parentUp = false;
      }
    }

    if (parentUp == true && host_stat != HOST_STATUS_DOWN) {
      Debug("parent_select", "status for %s: %d", parents[cur_index].hostname, host_stat);
      result->result      = PARENT_SPECIFIED;
      result->hostname    = parents[cur_index].hostname;
      result->port        = parents[cur_index].port;
      result->last_parent = cur_index;
      result->retry       = parentRetry;
      ink_assert(result->hostname != nullptr);
      ink_assert(result->port != 0);
      Debug("parent_select", "Chosen parent = %s.%d", result->hostname, result->port);
      return;
    }
    latched_parent = cur_index = (cur_index + 1) % num_parents;
  } while (static_cast<unsigned int>(cur_index) != result->start_parent);

  if (result->rec->go_direct == true && result->rec->parent_is_proxy == true) {
    result->result = PARENT_DIRECT;
  } else {
    result->result = PARENT_FAIL;
  }

  result->hostname = nullptr;
  result->port     = 0;
}

uint32_t
ParentRoundRobin::numParents(ParentResult *result) const
{
  return num_parents;
}
