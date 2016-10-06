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
#include "ParentRoundRobin.h"

ParentRoundRobin::ParentRoundRobin(ParentRecord *parent_record, ParentRR_t _round_robin_type)
{
  round_robin_type = _round_robin_type;
  latched_parent   = 0;

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

ParentRoundRobin::~ParentRoundRobin()
{
}

void
ParentRoundRobin::selectParent(const ParentSelectionPolicy *policy, bool first_call, ParentResult *result, RequestData *rdata)
{
  Debug("parent_select", "In ParentRoundRobin::selectParent(): Using a round robin parent selection strategy.");
  int cur_index    = 0;
  bool parentUp    = false;
  bool parentRetry = false;

  HttpRequestData *request_info = static_cast<HttpRequestData *>(rdata);

  ink_assert(numParents(result) > 0 || result->rec->go_direct == true);

  if (first_call) {
    if (result->rec->parents == NULL) {
      // We should only get into this state if
      //   if we are supposed to go direct
      ink_assert(result->rec->go_direct == true);
      // Could not find a parent
      if (result->rec->go_direct == true && result->rec->parent_is_proxy == true) {
        result->result = PARENT_DIRECT;
      } else {
        result->result = PARENT_FAIL;
      }

      result->hostname = NULL;
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
        if (rdata->get_client_ip() != NULL) {
          cur_index = result->start_parent = ntohl(ats_ip_hash(rdata->get_client_ip())) % result->rec->num_parents;
        } else {
          cur_index = 0;
        }
        break;
      case P_STRICT_ROUND_ROBIN:
        cur_index = ink_atomic_increment((int32_t *)&result->rec->rr_next, 1);
        cur_index = result->start_parent = cur_index % result->rec->num_parents;
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
    latched_parent = cur_index = (result->last_parent + 1) % result->rec->num_parents;

    // Check to see if we have wrapped around
    if ((unsigned int)cur_index == result->start_parent) {
      // We've wrapped around so bypass if we can
      if (result->rec->go_direct == true) {
        // Could not find a parent
        if (result->rec->parent_is_proxy == true) {
          result->result = PARENT_DIRECT;
        } else {
          result->result = PARENT_FAIL;
        }
        result->hostname = NULL;
        result->port     = 0;
        return;
      }
    }
  }
  // Loop through the array of parent seeing if any are up or
  //   should be retried
  do {
    Debug("parent_select", "cur_index: %d, result->start_parent: %d", cur_index, result->start_parent);
    // DNS ParentOnly inhibits bypassing the parent so always return that t
    if ((result->rec->parents[cur_index].failedAt == 0) || (result->rec->parents[cur_index].failCount < policy->FailThreshold)) {
      Debug("parent_select", "FailThreshold = %d", policy->FailThreshold);
      Debug("parent_select", "Selecting a parent due to little failCount (faileAt: %u failCount: %d)",
            (unsigned)result->rec->parents[cur_index].failedAt, result->rec->parents[cur_index].failCount);
      parentUp = true;
    } else {
      if ((result->wrap_around) ||
          ((result->rec->parents[cur_index].failedAt + policy->ParentRetryTime) < request_info->xact_start)) {
        Debug("parent_select", "Parent[%d].failedAt = %u, retry = %u,xact_start = %" PRId64 " but wrap = %d", cur_index,
              (unsigned)result->rec->parents[cur_index].failedAt, policy->ParentRetryTime, (int64_t)request_info->xact_start,
              result->wrap_around);
        // Reuse the parent
        parentUp    = true;
        parentRetry = true;
        Debug("parent_select", "Parent marked for retry %s:%d", result->rec->parents[cur_index].hostname,
              result->rec->parents[cur_index].port);
      } else {
        parentUp = false;
      }
    }

    if (parentUp == true) {
      result->result      = PARENT_SPECIFIED;
      result->hostname    = result->rec->parents[cur_index].hostname;
      result->port        = result->rec->parents[cur_index].port;
      result->last_parent = cur_index;
      result->retry       = parentRetry;
      ink_assert(result->hostname != NULL);
      ink_assert(result->port != 0);
      Debug("parent_select", "Chosen parent = %s.%d", result->hostname, result->port);
      return;
    }
    latched_parent = cur_index = (cur_index + 1) % result->rec->num_parents;
  } while ((unsigned int)cur_index != result->start_parent);

  if (result->rec->go_direct == true && result->rec->parent_is_proxy == true) {
    result->result = PARENT_DIRECT;
  } else {
    result->result = PARENT_FAIL;
  }

  result->hostname = NULL;
  result->port     = 0;
}

uint32_t
ParentRoundRobin::numParents(ParentResult *result) const
{
  return result->rec->num_parents;
}

void
ParentRoundRobin::markParentDown(const ParentSelectionPolicy *policy, ParentResult *result)
{
  time_t now;
  pRecord *pRec;
  int new_fail_count = 0;

  Debug("parent_select", "Starting ParentRoundRobin::markParentDown()");
  //  Make sure that we are being called back with with a
  //   result structure with a parent
  ink_assert(result->result == PARENT_SPECIFIED);
  if (result->result != PARENT_SPECIFIED) {
    return;
  }
  // If we were set through the API we currently have not failover
  //   so just return fail
  if (result->is_api_result()) {
    return;
  }

  ink_assert((int)(result->last_parent) < result->rec->num_parents);
  pRec = result->rec->parents + result->last_parent;

  // If the parent has already been marked down, just increment
  //   the failure count.  If this is the first mark down on a
  //   parent we need to both set the failure time and set
  //   count to one.  It's possible for the count and time get out
  //   sync due there being no locks.  Therefore the code should
  //   handle this condition.  If this was the result of a retry, we
  //   must update move the failedAt timestamp to now so that we continue
  //   negative cache the parent
  if (pRec->failedAt == 0 || result->retry == true) {
    // Reread the current time.  We want this to be accurate since
    //   it relates to how long the parent has been down.
    now = time(NULL);

    // Mark the parent as down
    ink_atomic_swap(&pRec->failedAt, now);

    // If this is clean mark down and not a failed retry, we
    //   must set the count to reflect this
    if (result->retry == false) {
      new_fail_count = pRec->failCount = 1;
    }

    Note("Parent %s marked as down %s:%d", (result->retry) ? "retry" : "initially", pRec->hostname, pRec->port);

  } else {
    int old_count = ink_atomic_increment(&pRec->failCount, 1);

    Debug("parent_select", "Parent fail count increased to %d for %s:%d", old_count + 1, pRec->hostname, pRec->port);
    new_fail_count = old_count + 1;
  }

  if (new_fail_count > 0 && new_fail_count >= policy->FailThreshold) {
    Note("Failure threshold met, http parent proxy %s:%d marked down", pRec->hostname, pRec->port);
    ink_atomic_swap(&pRec->available, false);
    Debug("parent_select", "Parent marked unavailable, pRec->available=%d", pRec->available);
  }
}

void
ParentRoundRobin::markParentUp(ParentResult *result)
{
  pRecord *pRec;

  //  Make sure that we are being called back with with a
  //   result structure with a parent that is being retried
  ink_release_assert(result->retry == true);
  ink_assert(result->result == PARENT_SPECIFIED);
  if (result->result != PARENT_SPECIFIED) {
    return;
  }
  // If we were set through the API we currently have not failover
  //   so just return fail
  if (result->is_api_result()) {
    ink_assert(0);
    return;
  }

  ink_assert((int)(result->last_parent) < result->rec->num_parents);
  pRec = result->rec->parents + result->last_parent;
  ink_atomic_swap(&pRec->available, true);

  ink_atomic_swap(&pRec->failedAt, (time_t)0);
  int old_count = ink_atomic_swap(&pRec->failCount, 0);

  if (old_count > 0) {
    Note("http parent proxy %s:%d restored", pRec->hostname, pRec->port);
  }
}
