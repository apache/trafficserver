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
#include "ParentConsistentHash.h"

ParentConsistentHash::ParentConsistentHash(ParentRecord *parent_record)
{
  int i;

  ink_assert(parent_record->num_parents > 0);
  parents[PRIMARY]   = parent_record->parents;
  parents[SECONDARY] = parent_record->secondary_parents;
  ignore_query       = parent_record->ignore_query;
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
  uint64_t path_hash            = 0;
  uint32_t last_lookup;
  pRecord *prtmp = nullptr, *pRec = nullptr;
  HostStatus &pStatus = HostStatus::instance();

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

  // findParent() call if firstCall.
  if (firstCall) {
    last_lookup = PRIMARY;
    path_hash   = getPathHash(request_info, (ATSHash64 *)&hash);
    fhash       = chash[PRIMARY];
    if (path_hash) {
      prtmp = (pRecord *)fhash->lookup_by_hashval(path_hash, &result->chashIter[last_lookup], &wrap_around[last_lookup]);
      if (prtmp) {
        pRec = (parents[last_lookup] + prtmp->idx);
      }
    }
    // else called by nextParent().
  } else {
    if (chash[SECONDARY] != nullptr) {
      last_lookup = SECONDARY;
      fhash       = chash[SECONDARY];
      path_hash   = getPathHash(request_info, (ATSHash64 *)&hash);
      prtmp       = (pRecord *)fhash->lookup_by_hashval(path_hash, &result->chashIter[last_lookup], &wrap_around[last_lookup]);
      if (prtmp) {
        pRec = (parents[last_lookup] + prtmp->idx);
      }
    } else {
      last_lookup = PRIMARY;
      fhash       = chash[PRIMARY];
      do { // search until we've selected a different parent.
        prtmp = (pRecord *)fhash->lookup(nullptr, &result->chashIter[last_lookup], &wrap_around[last_lookup], &hash);
        if (prtmp) {
          pRec = (parents[last_lookup] + prtmp->idx);
        } else {
          pRec = nullptr;
        }
      } while (prtmp && strcmp(prtmp->hostname, result->hostname) == 0);
    }
  }
  // didn't find a parent or the parent is marked unavailable.
  if ((pRec && !pRec->available) || pStatus.getHostStatus(pRec->hostname) == HOST_STATUS_DOWN) {
    do {
      if (pRec && !pRec->available) {
        Debug("parent_select", "Parent.failedAt = %u, retry = %u, xact_start = %u", (unsigned int)pRec->failedAt,
              (unsigned int)retry_time, (unsigned int)request_info->xact_start);
        if ((pRec->failedAt + retry_time) < request_info->xact_start) {
          parentRetry = true;
          // make sure that the proper state is recorded in the result structure
          result->last_parent = pRec->idx;
          result->last_lookup = last_lookup;
          result->retry       = parentRetry;
          result->result      = PARENT_SPECIFIED;
          Debug("parent_select", "Down parent %s is now retryable, marked it available.", pRec->hostname);
          break;
        }
      }
      Debug("parent_select", "wrap_around[PRIMARY]: %d, wrap_around[SECONDARY]: %d", wrap_around[PRIMARY], wrap_around[SECONDARY]);
      if (!wrap_around[PRIMARY] || (chash[SECONDARY] != nullptr)) {
        Debug("parent_select", "Selected parent %s is not available, looking up another parent.", pRec ? pRec->hostname : "[NULL]");
        if (chash[SECONDARY] != nullptr && !wrap_around[SECONDARY]) {
          fhash       = chash[SECONDARY];
          last_lookup = SECONDARY;
        } else {
          fhash       = chash[PRIMARY];
          last_lookup = PRIMARY;
        }
        if (firstCall) {
          prtmp     = (pRecord *)fhash->lookup_by_hashval(path_hash, &result->chashIter[last_lookup], &wrap_around[last_lookup]);
          firstCall = false;
        } else {
          prtmp = (pRecord *)fhash->lookup(nullptr, &result->chashIter[last_lookup], &wrap_around[last_lookup], &hash);
        }

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
    } while (!prtmp || !pRec->available || pStatus.getHostStatus(pRec->hostname) == HOST_STATUS_DOWN);
  }

  // use the available or marked for retry parent.
  if (pRec && (pRec->available || result->retry)) {
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

  ink_assert((result->last_parent) < numParents(result));
  pRec = parents[result->last_lookup] + result->last_parent;
  ink_atomic_swap(&pRec->available, true);
  Debug("parent_select", "%s:%s(): marked %s:%d available.", __FILE__, __func__, pRec->hostname, pRec->port);

  ink_atomic_swap(&pRec->failedAt, (time_t)0);
  int old_count = ink_atomic_swap(&pRec->failCount, 0);

  if (old_count > 0) {
    Note("http parent proxy %s:%d restored", pRec->hostname, pRec->port);
  }
}
