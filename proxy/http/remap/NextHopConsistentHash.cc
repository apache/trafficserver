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

#include <yaml-cpp/yaml.h>

#include "tscore/HashSip.h"
#include "HttpSM.h"
#include "NextHopConsistentHash.h"

// hash_key strings.
constexpr std::string_view hash_key_url           = "url";
constexpr std::string_view hash_key_hostname      = "hostname";
constexpr std::string_view hash_key_path          = "path";
constexpr std::string_view hash_key_path_query    = "path+query";
constexpr std::string_view hash_key_path_fragment = "path+fragment";
constexpr std::string_view hash_key_cache         = "cache_key";

static HostRecord *
chash_lookup(const std::shared_ptr<ATSConsistentHash> &ring, uint64_t hash_key, ATSConsistentHashIter *iter, bool *wrapped,
             ATSHash64Sip24 *hash, bool *hash_init, bool *mapWrapped, uint64_t sm_id)
{
  HostRecord *host_rec = nullptr;

  if (*hash_init == false) {
    host_rec   = static_cast<HostRecord *>(ring->lookup_by_hashval(hash_key, iter, wrapped));
    *hash_init = true;
  } else {
    host_rec = static_cast<HostRecord *>(ring->lookup(nullptr, iter, wrapped, hash));
  }
  bool wrap_around = *wrapped;
  *wrapped         = (*mapWrapped && *wrapped) ? true : false;
  if (!*mapWrapped && wrap_around) {
    *mapWrapped = true;
  }

  return host_rec;
}

NextHopConsistentHash::~NextHopConsistentHash()
{
  NH_Debug(NH_DEBUG_TAG, "destructor called for strategy named: %s", strategy_name.c_str());
}

bool
NextHopConsistentHash::Init(const YAML::Node &n)
{
  ATSHash64Sip24 hash;

  try {
    if (n["hash_key"]) {
      auto hash_key_val = n["hash_key"].Scalar();
      if (hash_key_val == hash_key_url) {
        hash_key = NH_URL_HASH_KEY;
      } else if (hash_key_val == hash_key_hostname) {
        hash_key = NH_HOSTNAME_HASH_KEY;
      } else if (hash_key_val == hash_key_path) {
        hash_key = NH_PATH_HASH_KEY;
      } else if (hash_key_val == hash_key_path_query) {
        hash_key = NH_PATH_QUERY_HASH_KEY;
      } else if (hash_key_val == hash_key_path_fragment) {
        hash_key = NH_PATH_FRAGMENT_HASH_KEY;
      } else if (hash_key_val == hash_key_cache) {
        hash_key = NH_CACHE_HASH_KEY;
      } else {
        hash_key = NH_PATH_HASH_KEY;
        NH_Note("Invalid 'hash_key' value, '%s', for the strategy named '%s', using default '%s'.", hash_key_val.c_str(),
                strategy_name.c_str(), hash_key_path.data());
      }
    }
  } catch (std::exception &ex) {
    NH_Note("Error parsing the strategy named '%s' due to '%s', this strategy will be ignored.", strategy_name.c_str(), ex.what());
    return false;
  }

  bool result = NextHopSelectionStrategy::Init(n);
  if (!result) {
    return false;
  }

  // load up the hash rings.
  for (uint32_t i = 0; i < groups; i++) {
    std::shared_ptr<ATSConsistentHash> hash_ring = std::make_shared<ATSConsistentHash>();
    for (uint32_t j = 0; j < host_groups[i].size(); j++) {
      // ATSConsistentHash needs the raw pointer.
      HostRecord *p = host_groups[i][j].get();
      // need to copy the 'hash_string' or 'hostname' cstring to 'name' for insertion into ATSConsistentHash.
      if (!p->hash_string.empty()) {
        p->name = const_cast<char *>(p->hash_string.c_str());
      } else {
        p->name = const_cast<char *>(p->hostname.c_str());
      }
      p->group_index = host_groups[i][j]->group_index;
      p->host_index  = host_groups[i][j]->host_index;
      hash_ring->insert(p, p->weight, &hash);
      NH_Debug(NH_DEBUG_TAG, "Loading hash rings - ring: %d, host record: %d, name: %s, hostname: %s, stategy: %s", i, j, p->name,
               p->hostname.c_str(), strategy_name.c_str());
    }
    hash.clear();
    rings.push_back(std::move(hash_ring));
  }
  return true;
}

// returns a hash key calculated from the request and 'hash_key' configuration
// parameter.
uint64_t
NextHopConsistentHash::getHashKey(uint64_t sm_id, HttpRequestData *hrdata, ATSHash64 *h)
{
  URL *url                   = hrdata->hdr->url_get();
  URL *ps_url                = nullptr;
  int len                    = 0;
  const char *url_string_ref = nullptr;

  // calculate a hash using the selected config.
  switch (hash_key) {
  case NH_URL_HASH_KEY:
    url_string_ref = url->string_get_ref(&len, URLNormalize::LC_SCHEME_HOST);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
      NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] url hash string: %s", sm_id, url_string_ref);
    }
    break;
  // hostname hash
  case NH_HOSTNAME_HASH_KEY:
    url_string_ref = url->host_get(&len);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
    }
    break;
  // path + query string
  case NH_PATH_QUERY_HASH_KEY:
    url_string_ref = url->path_get(&len);
    h->update("/", 1);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
    }
    url_string_ref = url->query_get(&len);
    if (url_string_ref && len > 0) {
      h->update("?", 1);
      h->update(url_string_ref, len);
    }
    break;
  // path + fragment hash
  case NH_PATH_FRAGMENT_HASH_KEY:
    url_string_ref = url->path_get(&len);
    h->update("/", 1);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
    }
    url_string_ref = url->fragment_get(&len);
    if (url_string_ref && len > 0) {
      h->update("?", 1);
      h->update(url_string_ref, len);
    }
    break;
  // use the cache key created by the cache-key plugin.
  case NH_CACHE_HASH_KEY:
    ps_url = *(hrdata->cache_info_parent_selection_url);
    if (ps_url) {
      url_string_ref = ps_url->string_get_ref(&len);
      if (url_string_ref && len > 0) {
        NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] using parent selection over-ride string:'%.*s'.", sm_id, len, url_string_ref);
        h->update(url_string_ref, len);
      }
    } else {
      url_string_ref = url->path_get(&len);
      h->update("/", 1);
      if (url_string_ref && len > 0) {
        NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] the parent selection over-ride url is not set, using default path: %s.", sm_id,
                 url_string_ref);
        h->update(url_string_ref, len);
      }
    }
    break;
  // use the path as the hash, default.
  case NH_PATH_HASH_KEY:
  default:
    url_string_ref = url->path_get(&len);
    h->update("/", 1);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
    }
    break;
  }

  h->final();
  return h->get();
}

void
NextHopConsistentHash::findNextHop(TSHttpTxn txnp, void *ih, time_t now)
{
  HttpSM *sm                   = reinterpret_cast<HttpSM *>(txnp);
  ParentResult *result         = &sm->t_state.parent_result;
  HttpRequestData request_info = sm->t_state.request_data;
  int64_t sm_id                = sm->sm_id;
  int64_t retry_time           = sm->t_state.txn_conf->parent_retry_time;
  time_t _now                  = now;
  bool firstcall               = false;
  bool nextHopRetry            = false;
  bool wrapped                 = false;
  std::vector<bool> wrap_around(groups, false);
  uint32_t cur_ring = 0; // there is a hash ring for each host group
  uint64_t hash_key = 0;
  uint32_t lookups  = 0;
  ATSHash64Sip24 hash;
  HostRecord *hostRec              = nullptr;
  std::shared_ptr<HostRecord> pRec = nullptr;
  HostStatus &pStatus              = HostStatus::instance();
  HostStatus_t host_stat           = HostStatus_t::HOST_STATUS_INIT;
  HostStatRec *hst                 = nullptr;

  if (result->line_number == -1 && result->result == PARENT_UNDEFINED) {
    firstcall = true;
  }

  if (firstcall) {
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] firstcall, line_number: %d, result: %s", sm_id, result->line_number,
             ParentResultStr[result->result]);
    result->line_number = distance;
    cur_ring            = 0;
    for (uint32_t i = 0; i < groups; i++) {
      result->chash_init[i] = false;
      wrap_around[i]        = false;
    }
  } else {
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] not firstcall, line_number: %d, result: %s", sm_id, result->line_number,
             ParentResultStr[result->result]);
    switch (ring_mode) {
    case NH_ALTERNATE_RING:
      if (groups > 1) {
        cur_ring = (result->last_group + 1) % groups;
      } else {
        cur_ring = result->last_group;
      }
      break;
    case NH_EXHAUST_RING:
    default:
      if (!wrapped) {
        cur_ring = result->last_group;
      } else if (groups > 1) {
        cur_ring = (result->last_group + 1) % groups;
      }
      break;
    }
  }

  // Do the initial parent look-up.
  hash_key = getHashKey(sm_id, &request_info, &hash);

  do { // search until we've selected a different parent if !firstcall
    std::shared_ptr<ATSConsistentHash> r = rings[cur_ring];
    hostRec               = chash_lookup(r, hash_key, &result->chashIter[cur_ring], &wrapped, &hash, &result->chash_init[cur_ring],
                           &result->mapWrapped[cur_ring], sm_id);
    wrap_around[cur_ring] = wrapped;
    lookups++;
    // the 'available' flag is maintained in 'host_groups' and not the hash ring.
    if (hostRec) {
      pRec = host_groups[hostRec->group_index][hostRec->host_index];
      if (firstcall) {
        hst                         = (pRec) ? pStatus.getHostStatus(pRec->hostname.c_str()) : nullptr;
        result->first_choice_status = (hst) ? hst->status : HostStatus_t::HOST_STATUS_UP;
        break;
      }
    } else {
      pRec = nullptr;
    }
  } while (pRec && result->hostname && strcmp(pRec->hostname.c_str(), result->hostname) == 0);

  NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] Initial parent lookups: %d", sm_id, lookups);

  // ----------------------------------------------------------------------------------------------------
  // Validate initial parent look-up and perform additional look-ups if required.
  // ----------------------------------------------------------------------------------------------------

  hst       = (pRec) ? pStatus.getHostStatus(pRec->hostname.c_str()) : nullptr;
  host_stat = (hst) ? hst->status : HostStatus_t::HOST_STATUS_UP;
  // if the config ignore_self_detect is set to true and the host is down due to SELF_DETECT reason
  // ignore the down status and mark it as available
  if ((pRec && ignore_self_detect) && (hst && hst->status == HOST_STATUS_DOWN)) {
    if (hst->reasons == Reason::SELF_DETECT) {
      host_stat = HOST_STATUS_UP;
    }
  }
  if (!pRec || (pRec && !pRec->available) || host_stat == HOST_STATUS_DOWN) {
    do {
      // check if an unavailable server is now retryable, use it if it is.
      if (pRec && !pRec->available && host_stat == HOST_STATUS_UP) {
        _now == 0 ? _now = time(nullptr) : _now = now;
        // check if the host is retryable.  It's retryable if the retry window has elapsed
        if ((pRec->failedAt + retry_time) < static_cast<unsigned>(_now)) {
          nextHopRetry        = true;
          result->last_parent = pRec->host_index;
          result->last_lookup = pRec->group_index;
          result->retry       = nextHopRetry;
          result->result      = PARENT_SPECIFIED;
          NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] next hop %s is now retryable, marked it available.", sm_id, pRec->hostname.c_str());
          break;
        }
      }
      switch (ring_mode) {
      case NH_ALTERNATE_RING:
        if (pRec && groups > 0) {
          cur_ring = (pRec->group_index + 1) % groups;
        } else {
          cur_ring = 0;
        }
        break;
      case NH_EXHAUST_RING:
      default:
        if (wrap_around[cur_ring] && groups > 1) {
          cur_ring = (cur_ring + 1) % groups;
        }
        break;
      }
      std::shared_ptr<ATSConsistentHash> r = rings[cur_ring];
      hostRec = chash_lookup(r, hash_key, &result->chashIter[cur_ring], &wrapped, &hash, &result->chash_init[cur_ring],
                             &result->mapWrapped[cur_ring], sm_id);
      wrap_around[cur_ring] = wrapped;
      lookups++;
      if (hostRec) {
        pRec      = host_groups[hostRec->group_index][hostRec->host_index];
        hst       = (pRec) ? pStatus.getHostStatus(pRec->hostname.c_str()) : nullptr;
        host_stat = (hst) ? hst->status : HostStatus_t::HOST_STATUS_UP;
        // if the config ignore_self_detect is set to true and the host is down due to SELF_DETECT reason
        // ignore the down status and mark it as available
        if ((pRec && ignore_self_detect) && (hst && hst->status == HOST_STATUS_DOWN)) {
          if (hst->reasons == Reason::SELF_DETECT) {
            host_stat = HOST_STATUS_UP;
          }
        }
        NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] Selected a new parent: %s, available: %s, wrapped: %s, lookups: %d.", sm_id,
                 pRec->hostname.c_str(), (pRec->available) ? "true" : "false", (wrapped) ? "true" : "false", lookups);
        // use available host.
        if (pRec->available && host_stat == HOST_STATUS_UP) {
          break;
        }
      } else {
        pRec = nullptr;
      }
      bool all_wrapped = true;
      for (uint32_t c = 0; c < groups; c++) {
        if (wrap_around[c] == false) {
          all_wrapped = false;
        }
      }
      if (all_wrapped) {
        NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] No available parents.", sm_id);
        if (pRec) {
          pRec = nullptr;
        }
        break;
      }
    } while (!pRec || (pRec && !pRec->available) || host_stat == HOST_STATUS_DOWN);
  }

  // ----------------------------------------------------------------------------------------------------
  // Validate and return the final result.
  // ----------------------------------------------------------------------------------------------------

  if (pRec && host_stat == HOST_STATUS_UP && (pRec->available || result->retry)) {
    result->result      = PARENT_SPECIFIED;
    result->hostname    = pRec->hostname.c_str();
    result->last_parent = pRec->host_index;
    result->last_lookup = result->last_group = cur_ring;
    switch (scheme) {
    case NH_SCHEME_NONE:
    case NH_SCHEME_HTTP:
      result->port = pRec->getPort(scheme);
      break;
    case NH_SCHEME_HTTPS:
      result->port = pRec->getPort(scheme);
      break;
    }
    result->retry = nextHopRetry;
    ink_assert(result->hostname != nullptr);
    ink_assert(result->port != 0);
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] result->result: %s Chosen parent: %s.%d", sm_id, ParentResultStr[result->result],
             result->hostname, result->port);
  } else {
    if (go_direct == true) {
      result->result = PARENT_DIRECT;
    } else {
      result->result = PARENT_FAIL;
    }
    result->hostname = nullptr;
    result->port     = 0;
    result->retry    = false;
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] result->result: %s set hostname null port 0 retry false", sm_id,
             ParentResultStr[result->result]);
  }

  return;
}
