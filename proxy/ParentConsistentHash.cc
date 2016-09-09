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
#include "ParentConsistentHash.h"
#include "ts/Diags.h"

ParentConsistentHash::ParentConsistentHash(ParentRecord *parent_record)
{
  int i;

  ink_assert(parent_record->num_parents > 0);
  parents[PRIMARY]   = parent_record->parents;
  parents[SECONDARY] = parent_record->secondary_parents;
  ignore_query       = parent_record->ignore_query;
  ignore_fname       = parent_record->ignore_fname;
  max_dirs           = parent_record->max_dirs;
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
    chash[SECONDARY] = NULL;
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
  const char *tmp = NULL;
  int len;
  URL *url     = hrdata->hdr->url_get();
  int num_dirs = 0;

  // Use over-ride URL from HttpTransact::State's cache_info.parent_selection_url, if present.
  URL *ps_url = NULL;
  Debug("parent_select", "hrdata->cache_info_parent_selection_url = %p", hrdata->cache_info_parent_selection_url);
  if (hrdata->cache_info_parent_selection_url) {
    ps_url = *(hrdata->cache_info_parent_selection_url);
    Debug("parent_select", "ps_url = %p", ps_url);
    if (ps_url) {
      tmp = ps_url->string_get_ref(&len);
      if (tmp && len > 0) {
        // Print the over-ride URL
        Debug("parent_select", "Using Over-Ride String='%.*s'.", len, tmp);
        h->update(tmp, len);
        h->final();
        return h->get();
      }
    }
  }

  // Always hash on '/' because paths returned by ATS are always stripped of it
  h->update("/", 1);

  tmp = url->path_get(&len);

  if (tmp && len > 0) {
    // Print the Original path.
    Debug("parent_select", "Original Path='%.*s'.", len, tmp);

    // Process the 'maxdirs' directive.
    if (max_dirs != 0) {
      // Determine number of directory components in the path.
      // NOTE: Leading '/' is gone already.
      for (int x = 0; x < len; x++) {
        if (tmp[x] == '/')
          num_dirs++;
      }
      // If max_dirs positive , include directory components from the left up to max_dirs.
      // If max_dirs negative , include directory components from the left up to num_dirs - ( abs(max_dirs) - 1 ).
      int limit = 0;
      if (max_dirs > 0)
        limit = max_dirs;
      else if (max_dirs < 0) {
        int md = abs(max_dirs) - 1;
        if (md < num_dirs)
          limit = num_dirs - md;
        else
          limit = 0;
      }
      if (limit > 0) {
        int x     = 0;
        int count = 0;
        for (; x < len; x++) {
          if (tmp[x] == '/')
            count++;
          if (count == limit) {
            len = x + 1;
            break;
          }
        }
      } else {
        len = 0;
      }
    }

    // Print the post 'maxdirs' path.
    Debug("parent_select", "Post-maxdirs Path='%.*s'.", len, tmp);

    // Process the 'fname' directive.
    // The file name (if any) is filtered out if set to ignore the file name or max_dirs was non-zero.
    // The file name (if any) consists of the characters at the end of the path beyond the final '/'.
    // The length of the path string (to be passed to the hash generator) is shortened to accomplish the filtering.
    if (ignore_fname || max_dirs != 0) {
      for (int x = len - 1; x >= 0; x--) {
        if (tmp[x] == '/') {
          len = x + 1;
          break;
        }
      }
    }

    // Print the post 'fname' path.
    Debug("parent_select", "Post-fname Path='%.*s'.", len, tmp);

    h->update(tmp, len);
  }

  // Process the 'qstring' directive.
  // The query string (if any) is not used if set to ignore the query string or set to ignore the file name or
  // max_dirs is non-zero.
  if (!ignore_query && !ignore_fname && max_dirs == 0) {
    tmp = url->query_get(&len);
    if (tmp) {
      h->update("?", 1);
      h->update(tmp, len);
      // Print the query string if used.
      Debug("parent_select", "Query='%.*s'.", len, tmp);
    }
  }

  h->final();

  return h->get();
}

void
ParentConsistentHash::selectParent(const ParentSelectionPolicy *policy, bool first_call, ParentResult *result, RequestData *rdata)
{
  ATSHash64Sip24 hash;
  ATSConsistentHash *fhash;
  HttpRequestData *request_info = static_cast<HttpRequestData *>(rdata);
  bool firstCall                = first_call;
  bool parentRetry              = false;
  bool wrap_around[2]           = {false, false};
  uint64_t path_hash            = 0;
  uint32_t last_lookup;
  pRecord *prtmp = NULL, *pRec = NULL;

  Debug("parent_select", "ParentConsistentHash::%s(): Using a consistent hash parent selection strategy.", __func__);
  ink_assert(numParents(result) > 0 || result->rec->go_direct == true);

  // Should only get into this state if we are supposed to go direct.
  if (parents[PRIMARY] == NULL && parents[SECONDARY] == NULL) {
    if (result->rec->go_direct == true && result->rec->parent_is_proxy == true) {
      result->result = PARENT_DIRECT;
    } else {
      result->result = PARENT_FAIL;
    }
    result->hostname = NULL;
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
    if (chash[SECONDARY] != NULL) {
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
        prtmp = (pRecord *)fhash->lookup(NULL, &result->chashIter[last_lookup], &wrap_around[last_lookup], &hash);
        if (prtmp) {
          pRec = (parents[last_lookup] + prtmp->idx);
        }
      } while (prtmp && strcmp(prtmp->hostname, result->hostname) == 0);
    }
  }

  // didn't find a parent or the parent is marked unavailable.
  if (!pRec || (pRec && !pRec->available)) {
    do {
      if (pRec && !pRec->available) {
        Debug("parent_select", "Parent.failedAt = %u, retry = %u, xact_start = %u", (unsigned int)pRec->failedAt,
              (unsigned int)policy->ParentRetryTime, (unsigned int)request_info->xact_start);
        if ((pRec->failedAt + policy->ParentRetryTime) < request_info->xact_start) {
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
      if (!wrap_around[PRIMARY] || (chash[SECONDARY] != NULL)) {
        Debug("parent_select", "Selected parent %s is not available, looking up another parent.", pRec->hostname);
        if (chash[SECONDARY] != NULL && !wrap_around[SECONDARY]) {
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
          prtmp = (pRecord *)fhash->lookup(NULL, &result->chashIter[last_lookup], &wrap_around[last_lookup], &hash);
        }

        if (prtmp) {
          pRec = (parents[last_lookup] + prtmp->idx);
          Debug("parent_select", "Selected a new parent: %s.", pRec->hostname);
        }
      }
      if (wrap_around[PRIMARY] && chash[SECONDARY] == NULL) {
        Debug("parent_select", "No available parents.");
        break;
      }
      if (wrap_around[PRIMARY] && chash[SECONDARY] != NULL && wrap_around[SECONDARY]) {
        Debug("parent_select", "No available parents.");
        break;
      }
    } while (!prtmp || !pRec->available);
  }

  // use the available or marked for retry parent.
  if (pRec && (pRec->available || result->retry)) {
    result->result      = PARENT_SPECIFIED;
    result->hostname    = pRec->hostname;
    result->port        = pRec->port;
    result->last_parent = pRec->idx;
    result->last_lookup = last_lookup;
    result->retry       = parentRetry;
    ink_assert(result->hostname != NULL);
    ink_assert(result->port != 0);
    Debug("parent_select", "Chosen parent: %s.%d", result->hostname, result->port);
  } else {
    if (result->rec->go_direct == true && result->rec->parent_is_proxy == true) {
      result->result = PARENT_DIRECT;
    } else {
      result->result = PARENT_FAIL;
    }
    result->hostname = NULL;
    result->port     = 0;
    result->retry    = false;
  }

  return;
}

void
ParentConsistentHash::markParentDown(const ParentSelectionPolicy *policy, ParentResult *result)
{
  time_t now;
  pRecord *pRec;
  int new_fail_count = 0;

  Debug("parent_select", "Starting ParentConsistentHash::markParentDown()");

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

  ink_assert((result->last_parent) < numParents(result));
  pRec = parents[result->last_lookup] + result->last_parent;

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
    Debug("parent_select", "Parent %s:%d marked unavailable, pRec->available=%d", pRec->hostname, pRec->port, pRec->available);
  }
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
