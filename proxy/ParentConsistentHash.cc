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
#include <atomic>
#include "HostStatus.h"
#include "ParentConsistentHash.h"

ParentConsistentHash::ParentConsistentHash(ParentRecord *parent_record)
{
  int i;

  ink_assert(parent_record->num_parents > 0);
  parents[PRIMARY]   = parent_record->parents;
  parents[SECONDARY] = parent_record->secondary_parents;
  ignore_query       = parent_record->ignore_query;
  secondary_mode     = parent_record->secondary_mode;
  ink_zero(foundParents);

  chash[PRIMARY] = new ATSConsistentHash();

  for (i = 0; i < parent_record->num_parents; i++) {
    chash[PRIMARY]->insert(&(parent_record->parents[i]), parent_record->parents[i].weight, (ATSHash64 *)&hash[PRIMARY]);
  }

  if (parent_record->num_secondary_parents > 0) {
    Debug("parent_select", "ParentConsistentHash(): initializing the secondary parents hash.");
    chash[SECONDARY] = new ATSConsistentHash();

    for (i = 0; i < parent_record->num_secondary_parents; i++) {
      chash[SECONDARY]->insert(&(parent_record->secondary_parents[i]), parent_record->secondary_parents[i].weight,
                               (ATSHash64 *)&hash[SECONDARY]);
    }
  } else {
    chash[SECONDARY] = nullptr;
  }
  Debug("parent_select", "Using a consistent hash parent selection strategy.");
}

ParentConsistentHash::~ParentConsistentHash()
{
  Debug("parent_select", "~ParentConsistentHash(): releasing hashes");
  delete chash[PRIMARY];
  delete chash[SECONDARY];
}

uint64_t
ParentConsistentHash::getPathHash(HttpRequestData *hrdata, ATSHash64 *h)
{
  const char *url_string_ref = nullptr;
  int len;
  URL *url = hrdata->hdr->url_get();

  // Use over-ride URL from HttpTransact::State's cache_info.parent_selection_url, if present.
  URL *ps_url = nullptr;
  if (hrdata->cache_info_parent_selection_url) {
    ps_url = *(hrdata->cache_info_parent_selection_url);
    if (ps_url) {
      url_string_ref = ps_url->string_get_ref(&len);
      if (url_string_ref && len > 0) {
        // Print the over-ride URL
        Debug("parent_select", "Using Over-Ride String='%.*s'.", len, url_string_ref);
        h->update(url_string_ref, len);
        h->final();
        return h->get();
      }
    }
  }

  // Always hash on '/' because paths returned by ATS are always stripped of it
  h->update("/", 1);

  url_string_ref = url->path_get(&len);
  if (url_string_ref) {
    h->update(url_string_ref, len);
  }

  if (!ignore_query) {
    url_string_ref = url->query_get(&len);
    if (url_string_ref) {
      h->update("?", 1);
      h->update(url_string_ref, len);
    }
  }

  h->final();

  return h->get();
}

// Helper function to abstract calling ATSConsistentHash lookup_by_hashval() vs lookup().
static pRecord *
chash_lookup(ATSConsistentHash *fhash, uint64_t path_hash, ATSConsistentHashIter *chashIter, bool *wrap_around,
             ATSHash64Sip24 *hash, bool *chash_init, bool *mapWrapped)
{
  pRecord *prtmp;

  if (*chash_init == false) {
    prtmp       = (pRecord *)fhash->lookup_by_hashval(path_hash, chashIter, wrap_around);
    *chash_init = true;
  } else {
    prtmp = (pRecord *)fhash->lookup(nullptr, chashIter, wrap_around, hash);
  }
  // Do not set wrap_around to true until we try all the parents at least once.
  bool wrapped = *wrap_around;
  *wrap_around = (*mapWrapped && *wrap_around) ? true : false;
  if (!*mapWrapped && wrapped) {
    *mapWrapped = true;
  }
  return prtmp;
}

void
ParentConsistentHash::selectParent(bool first_call, ParentResult *result, RequestData *rdata, unsigned int fail_threshold,
                                   unsigned int retry_time)
{
  ATSHash64Sip24 hash;
  ATSConsistentHash *fhash;
  HttpRequestData *request_info = static_cast<HttpRequestData *>(rdata);
  bool firstCall                = first_call;
  bool parentRetry              = false;
  bool wrap_around[2]           = {false, false};
  int lookups                   = 0;
  uint64_t path_hash            = 0;
  uint32_t last_lookup;
  pRecord *prtmp = nullptr, *pRec = nullptr;
  HostStatus &pStatus    = HostStatus::instance();
  TSHostStatus host_stat = TSHostStatus::TS_HOST_STATUS_INIT;

  Debug("parent_select", "ParentConsistentHash::%s(): Using a consistent hash parent selection strategy.", __func__);
  ink_assert(numParents(result) > 0 || result->rec->go_direct == true);

  // Should only get into this state if we are supposed to go direct.
  if (parents[PRIMARY] == nullptr && parents[SECONDARY] == nullptr) {
    if (result->rec->go_direct == true && result->rec->parent_is_proxy == true) {
      result->result = PARENT_DIRECT;
    } else {
      result->result = PARENT_FAIL;
    }
    result->hostname = nullptr;
    result->port     = 0;
    return;
  }

  // ----------------------------------------------------------------------------------------------------
  // Initial parent look-up for either findParent() (firstCall) or nextParent() (!firstCall)
  // ----------------------------------------------------------------------------------------------------

  // firstCall means called from findParent() and always use PRIMARY parent list.
  if (firstCall) {
    last_lookup = PRIMARY;
  } else {
    // !firstCall means called from nextParent() and must determine which parent list to use.
    switch (secondary_mode) {
    case 2:
      last_lookup = PRIMARY;
      break;
    case 3:
      if (result->first_choice_status == TS_HOST_STATUS_DOWN && chash[SECONDARY] != nullptr) {
        last_lookup = SECONDARY;
      } else {
        last_lookup = PRIMARY;
      }
      break;
    case 1:
    default:
      if (chash[SECONDARY] != nullptr) {
        last_lookup = SECONDARY;
      } else {
        last_lookup = PRIMARY;
      }
    }
  }

  // Do the initial parent look-up.
  path_hash = getPathHash(request_info, (ATSHash64 *)&hash);
  fhash     = chash[last_lookup];
  do { // search until we've selected a different parent if !firstCall
    prtmp = chash_lookup(fhash, path_hash, &result->chashIter[last_lookup], &wrap_around[last_lookup], &hash,
                         &result->chash_init[last_lookup], &result->mapWrapped[last_lookup]);
    lookups++;
    if (prtmp) {
      pRec = (parents[last_lookup] + prtmp->idx);
    } else {
      pRec = nullptr;
    }
    if (firstCall) {
      break;
    }
  } while (pRec && !firstCall && last_lookup == PRIMARY && strcmp(pRec->hostname, result->hostname) == 0);

  Debug("parent_select", "Initial parent lookups: %d", lookups);

  // ----------------------------------------------------------------------------------------------------
  // Validate initial parent look-up and perform additional look-ups if required.
  // ----------------------------------------------------------------------------------------------------

  // didn't find a parent or the parent is marked unavailable or the parent is marked down
  HostStatRec *hst = (pRec) ? pStatus.getHostStatus(pRec->hostname) : nullptr;
  host_stat        = (hst) ? hst->status : TSHostStatus::TS_HOST_STATUS_UP;
  if (firstCall) {
    result->first_choice_status = host_stat;
  }

  // if the config ignore_self_detect is set to true and the host is down due to SELF_DETECT reason
  // ignore the down status and mark it as available
  if ((pRec && result->rec->ignore_self_detect) && (hst && hst->status == TS_HOST_STATUS_DOWN)) {
    if (hst->reasons == Reason::SELF_DETECT) {
      host_stat = TS_HOST_STATUS_UP;
    }
  }
  if (!pRec || (pRec && !pRec->available.load()) || host_stat == TS_HOST_STATUS_DOWN) {
    do {
      // check if the host is retryable.  It's retryable if the retry window has elapsed
      // and the global host status is HOST_STATUS_UP
      if (pRec && !pRec->available.load() && host_stat == TS_HOST_STATUS_UP) {
        Debug("parent_select", "Parent.failedAt = %u, retry = %u, xact_start = %u", static_cast<unsigned>(pRec->failedAt.load()),
              static_cast<unsigned>(retry_time), static_cast<unsigned>(request_info->xact_start));
        if ((pRec->failedAt.load() + retry_time) < request_info->xact_start) {
          parentRetry = true;
          // make sure that the proper state is recorded in the result structure
          result->last_parent = pRec->idx;
          result->last_lookup = last_lookup;
          result->retry       = parentRetry;
          result->result      = PARENT_SPECIFIED;
          break;
        }
      }
      Debug("parent_select", "wrap_around[PRIMARY]: %d, wrap_around[SECONDARY]: %d", wrap_around[PRIMARY], wrap_around[SECONDARY]);
      if (!wrap_around[PRIMARY] || (chash[SECONDARY] != nullptr && !wrap_around[SECONDARY])) {
        Debug("parent_select", "Selected parent %s is not available, looking up another parent.", pRec ? pRec->hostname : "[NULL]");
        switch (secondary_mode) {
        case 2:
          if (!wrap_around[PRIMARY]) {
            last_lookup = PRIMARY;
          } else if (chash[SECONDARY] != nullptr && !wrap_around[SECONDARY]) {
            last_lookup = SECONDARY;
          }
          break;
        case 3:
          if (result->first_choice_status == TS_HOST_STATUS_DOWN) {
            if (chash[SECONDARY] != nullptr && !wrap_around[SECONDARY]) {
              last_lookup = SECONDARY;
            } else if (!wrap_around[PRIMARY]) {
              last_lookup = PRIMARY;
            }
          } else {
            if (!wrap_around[PRIMARY]) {
              last_lookup = PRIMARY;
            } else if (chash[SECONDARY] != nullptr && !wrap_around[SECONDARY]) {
              last_lookup = SECONDARY;
            }
          }
          break;
        case 1:
        default:
          if (chash[SECONDARY] != nullptr && !wrap_around[SECONDARY]) {
            last_lookup = SECONDARY;
          } else if (!wrap_around[PRIMARY]) {
            last_lookup = PRIMARY;
          }
        }
        fhash = chash[last_lookup];
        prtmp = chash_lookup(fhash, path_hash, &result->chashIter[last_lookup], &wrap_around[last_lookup], &hash,
                             &result->chash_init[last_lookup], &result->mapWrapped[last_lookup]);
        lookups++;
        if (prtmp) {
          pRec = (parents[last_lookup] + prtmp->idx);
          Debug("parent_select", "Selected a new parent: %s.", pRec->hostname);
        } else {
          pRec = nullptr;
        }
      }
      if (wrap_around[PRIMARY] && chash[SECONDARY] == nullptr) {
        Debug("parent_select", "No available parents.");
        break;
      }
      if (wrap_around[PRIMARY] && chash[SECONDARY] != nullptr && wrap_around[SECONDARY]) {
        Debug("parent_select", "No available parents.");
        break;
      }
      hst       = (pRec) ? pStatus.getHostStatus(pRec->hostname) : nullptr;
      host_stat = (hst) ? hst->status : TSHostStatus::TS_HOST_STATUS_UP;
      // if the config ignore_self_detect is set to true and the host is down due to SELF_DETECT reason
      // ignore the down status and mark it as available
      if ((pRec && result->rec->ignore_self_detect) && (hst && hst->status == TS_HOST_STATUS_DOWN)) {
        if (hst->reasons == Reason::SELF_DETECT) {
          host_stat = TS_HOST_STATUS_UP;
        }
      }
    } while (!pRec || !pRec->available.load() || host_stat == TS_HOST_STATUS_DOWN);
  }

  Debug("parent_select", "Additional parent lookups: %d", lookups);

  // ----------------------------------------------------------------------------------------------------
  // Validate and return the final result.
  // ----------------------------------------------------------------------------------------------------

  // if the config ignore_self_detect is set to true and the host is down due to SELF_DETECT reason
  // ignore the down status and mark it as available
  if ((pRec && result->rec->ignore_self_detect) && (hst && hst->status == TS_HOST_STATUS_DOWN)) {
    if (hst->reasons == Reason::SELF_DETECT) {
      host_stat = TS_HOST_STATUS_UP;
    }
  }
  if (pRec && host_stat == TS_HOST_STATUS_UP && (pRec->available.load() || result->retry)) {
    result->result      = PARENT_SPECIFIED;
    result->hostname    = pRec->hostname;
    result->port        = pRec->port;
    result->last_parent = pRec->idx;
    result->last_lookup = last_lookup;
    result->retry       = parentRetry;
    ink_assert(result->hostname != nullptr);
    ink_assert(result->port != 0);
    Debug("parent_select", "Chosen parent: %s.%d", result->hostname, result->port);
  } else {
    if (result->rec->go_direct == true && result->rec->parent_is_proxy == true) {
      result->result = PARENT_DIRECT;
    } else {
      result->result = PARENT_FAIL;
    }
    result->hostname = nullptr;
    result->port     = 0;
    result->retry    = false;
  }

  return;
}

uint32_t
ParentConsistentHash::numParents(ParentResult *result) const
{
  uint32_t n = 0;

  switch (result->last_lookup) {
  case PRIMARY:
    n = result->rec->num_parents;
    break;
  case SECONDARY:
    n = result->rec->num_secondary_parents;
    break;
  }

  return n;
}

void
ParentConsistentHash::markParentUp(ParentResult *result)
{
  pRecord *pRec;

  //  Make sure that we are being called back with a
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

  ink_assert((result->last_parent) < numParents(result));
  pRec            = parents[result->last_lookup] + result->last_parent;
  pRec->available = true;
  Debug("parent_select", "%s:%s(): marked %s:%d available.", __FILE__, __func__, pRec->hostname, pRec->port);

  pRec->failedAt = static_cast<time_t>(0);
  int old_count  = pRec->failCount.exchange(0, std::memory_order_relaxed);

  if (old_count > 0) {
    Note("http parent proxy %s:%d restored", pRec->hostname, pRec->port);
  }
}
