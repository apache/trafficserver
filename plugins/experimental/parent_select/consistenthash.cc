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

namespace
{
const char *PLNHParentResultStr[] = {"PARENT_UNDEFINED", "PARENT_DIRECT", "PARENT_SPECIFIED", "PARENT_AGENT", "PARENT_FAIL"};

// hash_key strings.
constexpr std::string_view hash_key_url           = "url";
constexpr std::string_view hash_key_hostname      = "hostname";
constexpr std::string_view hash_key_path          = "path";
constexpr std::string_view hash_key_path_query    = "path+query";
constexpr std::string_view hash_key_path_fragment = "path+fragment";
constexpr std::string_view hash_key_cache         = "cache_key";

static bool
isWrapped(std::vector<bool> &wrap_around, uint32_t groups)
{
  bool all_wrapped = true;
  for (uint32_t c = 0; c < groups; c++) {
    if (wrap_around[c] == false) {
      all_wrapped = false;
    }
  }
  return all_wrapped;
}

void
chTxnToStatusTxn(PLNextHopConsistentHashTxn *txn, PLStatusTxn *statusTxn)
{
  statusTxn->result = txn->result;
  statusTxn->retry  = txn->retry;
}

} // namespace

std::shared_ptr<PLHostRecord>
PLNextHopConsistentHash::chashLookup(const std::shared_ptr<ATSConsistentHash> &ring, uint32_t cur_ring,
                                     PLNextHopConsistentHashTxn *state, bool *wrapped, uint64_t sm_id, TSMBuffer reqp, TSMLoc url,
                                     TSMLoc parent_selection_url)
{
  uint64_t hash_key = 0;
  ATSHash64Sip24 hash;
  PLHostRecord *host_rec      = nullptr;
  ATSConsistentHashIter *iter = &state->chashIter[cur_ring];

  if (state->chash_init[cur_ring] == false) {
    hash_key                    = getHashKey(sm_id, reqp, url, parent_selection_url, &hash);
    host_rec                    = static_cast<PLHostRecord *>(ring->lookup_by_hashval(hash_key, iter, wrapped));
    state->chash_init[cur_ring] = true;
  } else {
    host_rec = static_cast<PLHostRecord *>(ring->lookup(nullptr, iter, wrapped, &hash));
  }
  bool wrap_around = *wrapped;
  *wrapped         = (state->mapWrapped[cur_ring] && *wrapped) ? true : false;
  if (!state->mapWrapped[cur_ring] && wrap_around) {
    state->mapWrapped[cur_ring] = true;
  }

  if (host_rec == nullptr) {
    return nullptr;
  } else {
    std::shared_ptr<PLHostRecord> h = host_groups[host_rec->group_index][host_rec->host_index];
    return h;
  }
}

PLNextHopConsistentHash::~PLNextHopConsistentHash()
{
  PL_NH_Debug(PL_NH_DEBUG_TAG, "destructor called for strategy named: %s", strategy_name.c_str());
}

#define PLUGIN_NAME "pparent_select"

PLNextHopConsistentHash::PLNextHopConsistentHash(const std::string_view name, const YAML::Node &n)
  : PLNextHopSelectionStrategy(name, n)
{
  TSDebug("pparent_select", "PLNextHopConsistentHash constructor calling.");

  ATSHash64Sip24 hash;

  try {
    if (n["hash_key"]) {
      auto hash_key_val = n["hash_key"].Scalar();
      if (hash_key_val == hash_key_url) {
        hash_key = PL_NH_URL_HASH_KEY;
      } else if (hash_key_val == hash_key_hostname) {
        hash_key = PL_NH_HOSTNAME_HASH_KEY;
      } else if (hash_key_val == hash_key_path) {
        hash_key = PL_NH_PATH_HASH_KEY;
      } else if (hash_key_val == hash_key_path_query) {
        hash_key = PL_NH_PATH_QUERY_HASH_KEY;
      } else if (hash_key_val == hash_key_path_fragment) {
        hash_key = PL_NH_PATH_FRAGMENT_HASH_KEY;
      } else if (hash_key_val == hash_key_cache) {
        hash_key = PL_NH_CACHE_HASH_KEY;
      } else {
        hash_key = PL_NH_PATH_HASH_KEY;
        PL_NH_Note("Invalid 'hash_key' value, '%s', for the strategy named '%s', using default '%s'.", hash_key_val.c_str(),
                   strategy_name.c_str(), hash_key_path.data());
      }
    }
  } catch (std::exception &ex) {
    throw std::invalid_argument("Error parsing the strategy named '" + strategy_name + "' due to '" + ex.what() +
                                "', this strategy will be ignored.");
  }

  // load up the hash rings.
  for (uint32_t i = 0; i < groups; i++) {
    std::shared_ptr<ATSConsistentHash> hash_ring = std::make_shared<ATSConsistentHash>();
    for (uint32_t j = 0; j < host_groups[i].size(); j++) {
      // ATSConsistentHash needs the raw pointer.
      PLHostRecord *p = host_groups[i][j].get();
      // need to copy the 'hash_string' or 'hostname' cstring to 'name' for insertion into ATSConsistentHash.
      if (!p->hash_string.empty()) {
        p->name = const_cast<char *>(p->hash_string.c_str());
      } else {
        p->name = const_cast<char *>(p->hostname.c_str());
      }
      p->group_index = host_groups[i][j]->group_index;
      p->host_index  = host_groups[i][j]->host_index;
      hash_ring->insert(p, p->weight, &hash);
      PL_NH_Debug(PL_NH_DEBUG_TAG, "Loading hash rings - ring: %d, host record: %d, name: %s, hostname: %s, stategy: %s", i, j,
                  p->name, p->hostname.c_str(), strategy_name.c_str());
    }
    hash.clear();
    rings.push_back(hash_ring);
  }

  if (ring_mode == PL_NH_PEERING_RING) {
    if (groups == 1) {
      if (!go_direct) {
        throw std::invalid_argument("ring mode '" + std::string(peering_rings) +
                                    "' go_direct must be true when there is only one host group");
      }
    } else if (groups != 2) {
      throw std::invalid_argument(
        "ring mode '" + std::string(peering_rings) +
        "' requires two host groups (peering group and an upstream group), or a single peering group with go_direct");
    }
  }
}

// returns a hash key calculated from the request and 'hash_key' configuration
// parameter.
uint64_t
PLNextHopConsistentHash::getHashKey(uint64_t sm_id, TSMBuffer reqp, TSMLoc url, TSMLoc parent_selection_url, ATSHash64 *h)
{
  int len                    = 0;
  const char *url_string_ref = nullptr;

  // calculate a hash using the selected config.
  switch (hash_key) {
  case PL_NH_URL_HASH_KEY:
    url_string_ref = TSUrlStringGet(reqp, url, &len);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
      PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] url hash string: %s", sm_id, url_string_ref);
    }
    break;
  // hostname hash
  case PL_NH_HOSTNAME_HASH_KEY:
    url_string_ref = TSUrlHostGet(reqp, url, &len);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
    }
    break;
  // path + query string
  case PL_NH_PATH_QUERY_HASH_KEY:
    url_string_ref = TSUrlPathGet(reqp, url, &len);
    h->update("/", 1);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
    }
    url_string_ref = TSUrlHttpQueryGet(reqp, url, &len);
    if (url_string_ref && len > 0) {
      h->update("?", 1);
      h->update(url_string_ref, len);
    }
    break;
  // path + fragment hash
  case PL_NH_PATH_FRAGMENT_HASH_KEY:
    url_string_ref = TSUrlPathGet(reqp, url, &len);
    h->update("/", 1);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
    }
    url_string_ref = TSUrlHttpFragmentGet(reqp, url, &len);
    if (url_string_ref && len > 0) {
      h->update("?", 1);
      h->update(url_string_ref, len);
    }
    break;
  // use the cache key created by the cache-key plugin.
  case PL_NH_CACHE_HASH_KEY:
    if (parent_selection_url != TS_NULL_MLOC) {
      url_string_ref = TSUrlStringGet(reqp, parent_selection_url, &len);
      if (url_string_ref && len > 0) {
        PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] using parent selection over-ride string:'%.*s'.", sm_id, len, url_string_ref);
        h->update(url_string_ref, len);
      }
    } else {
      url_string_ref = TSUrlPathGet(reqp, url, &len);
      h->update("/", 1);
      if (url_string_ref && len > 0) {
        PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] the parent selection over-ride url is not set, using default path: %s.", sm_id,
                    url_string_ref);
        h->update(url_string_ref, len);
      }
    }
    break;
  // use the path as the hash, default.
  case PL_NH_PATH_HASH_KEY:
  default:
    url_string_ref = TSUrlPathGet(reqp, url, &len);
    h->update("/", 1);
    if (url_string_ref && len > 0) {
      h->update(url_string_ref, len);
    }
    break;
  }

  h->final();
  return h->get();
}

static void
makeNextParentErr(const char **hostname, size_t *hostname_len, in_port_t *port, bool *retry, bool *no_cache)
{
  *hostname     = nullptr;
  *hostname_len = 0;
  *port         = 0;
  *retry        = false;
  *no_cache     = false;
}

void *
PLNextHopConsistentHash::newTxn()
{
  return new PLNextHopConsistentHashTxn();
}

void
PLNextHopConsistentHash::deleteTxn(void *txn)
{
  delete static_cast<PLNextHopConsistentHashTxn *>(txn);
}

void
PLNextHopConsistentHash::next(TSHttpTxn txnp, void *strategyTxn, const char **out_hostname, size_t *out_hostname_len,
                              in_port_t *out_port, bool *out_retry, bool *out_no_cache, time_t now)
{
  // TODO add logic in the strategy to track when someone is retrying, and not give it out to multiple threads at once, to prevent
  // thundering retries See github issue #7485

  PL_NH_Debug(PL_NH_DEBUG_TAG, "nextParent NH plugin calling");

  uint32_t const NO_RING_USE_POST_REMAP = uint32_t(0) - 1;

  auto state = static_cast<PLNextHopConsistentHashTxn *>(strategyTxn);

  int64_t sm_id = TSHttpTxnIdGet(txnp);

  TSMBuffer reqp; // TODO verify doesn't need freed

  TSMLoc hdr;
  ScopedFreeMLoc hdr_cleanup(&reqp, TS_NULL_MLOC, &hdr);
  if (TSHttpTxnClientReqGet(txnp, &reqp, &hdr) == TS_ERROR) {
    makeNextParentErr(out_hostname, out_hostname_len, out_port, out_retry, out_no_cache);
    return;
  }

  TSMLoc parent_selection_url = TS_NULL_MLOC;
  ScopedFreeMLoc parent_selection_url_cleanup(&reqp, TS_NULL_MLOC, &parent_selection_url);
  if (TSUrlCreate(reqp, &parent_selection_url) != TS_SUCCESS) {
    PL_NH_Error("nexthop failed to create url for parent_selection_url");
    makeNextParentErr(out_hostname, out_hostname_len, out_port, out_retry, out_no_cache);
    return;
  }
  if (TSHttpTxnParentSelectionUrlGet(txnp, reqp, parent_selection_url) != TS_SUCCESS) {
    parent_selection_url = TS_NULL_MLOC;
  }

  TSMLoc url;
  ScopedFreeMLoc url_cleanup(&reqp, hdr, &url);
  if (TSHttpHdrUrlGet(reqp, hdr, &url) != TS_SUCCESS) {
    PL_NH_Error("failed to get header url, cannot find next hop");
    makeNextParentErr(out_hostname, out_hostname_len, out_port, out_retry, out_no_cache);
    return;
  }

  // TODO is it really worth getting the string out to debug print here?
  PL_NH_Debug(PL_NH_DEBUG_TAG, "nextParent NH plugin findNextHop got url 'x'");

  int64_t retry_time = 0; //           = sm->t_state.txn_conf->parent_retry_time;
  if (TSHttpTxnConfigIntGet(txnp, TS_CONFIG_HTTP_PARENT_PROXY_RETRY_TIME, &retry_time) != TS_SUCCESS) {
    // TODO get and cache on init, to prevent potential runtime failure?
    PL_NH_Error("failed to get parent retry time, cannot find next hop");
    makeNextParentErr(out_hostname, out_hostname_len, out_port, out_retry, out_no_cache);
    return;
  }

  time_t _now       = now;
  bool nextHopRetry = false;
  bool wrapped      = false;
  std::vector<bool> wrap_around(groups, false);
  uint32_t cur_ring                  = 0; // there is a hash ring for each host group
  uint32_t lookups                   = 0;
  std::shared_ptr<PLHostRecord> pRec = nullptr;
  TSHostStatus host_stat             = TSHostStatus::TS_HOST_STATUS_INIT;
  std::string_view first_call_host;
  int first_call_port = 0;

  const bool firstcall = state->line_number == -1 && state->result == PL_NH_PARENT_UNDEFINED;

  // firstcall indicates that this is the first time the state machine has called findNextHop() for this
  // particular transaction so, a parent will be looked up using a hash from the request to locate a
  // parent on the consistent hash ring.  If not first call, then the transaction was unable to use the parent
  // returned from the "firstcall" due to some error so, subsequent calls will not search using a hash but,
  // will instead just increment the hash table iterator to find the next parent on the ring
  if (firstcall) {
    PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] firstcall, line_number: %d, result: %s", sm_id, state->line_number,
                PLNHParentResultStr[state->result]);
    state->line_number = distance;
    cur_ring           = 0;
    for (uint32_t i = 0; i < groups; i++) {
      state->chash_init[i] = false;
      wrap_around[i]       = false;
    }
  } else {
    // not first call, save the previously tried parent.
    if (state->hostname) {
      first_call_host = state->hostname;
      first_call_port = state->port;
    }
    PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] not firstcall, line_number: %d, result: %s", sm_id, state->line_number,
                PLNHParentResultStr[state->result]);
    switch (ring_mode) {
    case PL_NH_ALTERNATE_RING:
      if (groups > 1) {
        cur_ring = (state->last_group + 1) % groups;
      } else {
        cur_ring = state->last_group;
      }
      break;
    case PL_NH_PEERING_RING:
      if (groups == 1) {
        state->last_group = cur_ring = NO_RING_USE_POST_REMAP;
      } else {
        ink_assert(groups == 2);
        // look for the next parent on the
        // upstream ring.
        state->last_group = cur_ring = 1;
      }
      break;
    case PL_NH_EXHAUST_RING:
    default:
      if (!wrapped) {
        cur_ring = state->last_group;
      } else if (groups > 1) {
        cur_ring = (state->last_group + 1) % groups;
      }
      break;
    }
  }

  if (cur_ring != NO_RING_USE_POST_REMAP) {
    do {
      // all host groups have been searched and there are no available parents found
      if (isWrapped(wrap_around, groups)) {
        PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] No available parents.", sm_id);
        pRec = nullptr;
        break;
      }

      // search for available parent
      std::shared_ptr<ATSConsistentHash> r = rings[cur_ring];
      pRec                                 = chashLookup(r, cur_ring, state, &wrapped, sm_id, reqp, url, parent_selection_url);
      wrap_around[cur_ring]                = wrapped;
      lookups++;

      TSHostStatus hostStatus;
      unsigned int hostStatusReasons;
      const bool hostExists =
        pRec ? (TSHostStatusGet(pRec->hostname.c_str(), pRec->hostname.size(), &hostStatus, &hostStatusReasons) == TS_SUCCESS) :
               false;

      // found a parent
      if (pRec) {
        bool is_self = TSHostnameIsSelf(pRec->hostname.c_str(), pRec->hostname.size()) == TS_SUCCESS;
        host_stat    = hostExists ? hostStatus : TSHostStatus::TS_HOST_STATUS_UP;

        // if the config ignore_self_detect is set to true and the host is down due to SELF_DETECT reason
        // ignore the down status and mark it as available
        if ((host_stat == TS_HOST_STATUS_DOWN && is_self && ignore_self_detect)) {
          if (hostStatusReasons == TS_HOST_STATUS_SELF_DETECT) {
            host_stat = TS_HOST_STATUS_UP;
          }
        }

        if (firstcall) {
          state->first_choice_status = hostExists ? hostStatus : TSHostStatus::TS_HOST_STATUS_UP;
          // if peering and the selected host is myself, change rings and search for an upstream parent.
          if (ring_mode == PL_NH_PEERING_RING && (pRec->self || is_self)) {
            PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] peering ring got self %s - searching for upstream parent", sm_id,
                        pRec->hostname.c_str());
            if (groups == 1) {
              PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] peering ring got self %s - 1 group, using host from post-remap URL",
                          sm_id, pRec->hostname.c_str());
              // use host from post-remap URL
              cur_ring = NO_RING_USE_POST_REMAP;
              pRec     = nullptr;
              break;
            } else {
              PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] peering ring got self %s - !1 group, searching upstream ring", sm_id,
                          pRec->hostname.c_str());
              // switch to and search the upstream ring.
              cur_ring = 1;
              pRec     = nullptr;
              continue;
            }
          }
        } else {
          // not first call, make sure we're not re-using the same parent, search again if we are.
          if (first_call_host.size() > 0 && first_call_host == pRec->hostname && first_call_port == pRec->getPort(scheme)) {
            pRec = nullptr;
            continue;
          }
        }
        // if the parent is not available check to see if the retry window has expired making it available
        // for retry.
        if (!pRec->available.load() && host_stat == TS_HOST_STATUS_UP) {
          _now == 0 ? _now = time(nullptr) : _now = now;
          if ((pRec->failedAt.load() + retry_time) < static_cast<unsigned>(_now)) {
            nextHopRetry       = true;
            state->last_parent = pRec->host_index;
            state->last_lookup = pRec->group_index;
            state->retry       = nextHopRetry;
            state->result      = PL_NH_PARENT_SPECIFIED;
            state->no_cache    = false;
            PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] next hop %s is now retryable", sm_id, pRec->hostname.c_str());
            break;
          }
        }

        // use the available selected parent
        if (pRec->available.load() && host_stat == TS_HOST_STATUS_UP) {
          break;
        }
      }
      // try other rings per the ring mode
      switch (ring_mode) {
      case PL_NH_ALTERNATE_RING:
        if (pRec && groups > 0) {
          cur_ring = (pRec->group_index + 1) % groups;
        } else {
          cur_ring = 0;
        }
        break;
      case PL_NH_EXHAUST_RING:
      default:
        if (wrap_around[cur_ring] && groups > 1) {
          cur_ring = (cur_ring + 1) % groups;
        }
        break;
      }

      if (pRec) {
        // if the selected host is down, search again.
        if (!pRec->available || host_stat == TS_HOST_STATUS_DOWN) {
          PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] hostname: %s, available: %s, host_stat: %d", sm_id, pRec->hostname.c_str(),
                      pRec->available ? "true" : "false", host_stat);
          pRec = nullptr;
          continue;
        }
      }
    } while (!pRec);

    PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] Initial parent lookups: %d", sm_id, lookups);
  }

  // ----------------------------------------------------------------------------------------------------
  // Validate and return the final result.
  // ----------------------------------------------------------------------------------------------------

  if (pRec && host_stat == TS_HOST_STATUS_UP && (pRec->available.load() || state->retry)) {
    state->result       = PL_NH_PARENT_SPECIFIED;
    state->hostname     = pRec->hostname.c_str();
    state->hostname_len = pRec->hostname.size();
    state->last_parent  = pRec->host_index;
    state->last_lookup = state->last_group = cur_ring;
    switch (scheme) {
    case PL_NH_SCHEME_NONE:
    case PL_NH_SCHEME_HTTP:
      state->port = pRec->getPort(scheme);
      break;
    case PL_NH_SCHEME_HTTPS:
      state->port = pRec->getPort(scheme);
      break;
    }
    state->retry = nextHopRetry;
    // if using a peering ring mode and the parent selected came from the 'peering' group,
    // cur_ring == 0, then if the config allows it, set the flag to not cache the result.
    state->no_cache = ring_mode == PL_NH_PEERING_RING && !cache_peer_result && cur_ring == 0;
    PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] setting do not cache response from a peer per config: %s", sm_id,
                (state->no_cache) ? "true" : "false");
    ink_assert(state->hostname != nullptr);
    ink_assert(state->port != 0);
    PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] state->result: %s Chosen parent: %s.%d", sm_id, PLNHParentResultStr[state->result],
                state->hostname, state->port);
  } else {
    state->result       = go_direct ? PL_NH_PARENT_DIRECT : PL_NH_PARENT_FAIL;
    state->retry        = false;
    state->hostname     = nullptr;
    state->hostname_len = 0;
    state->port         = 0;
    state->no_cache     = false;
    PL_NH_Debug(PL_NH_DEBUG_TAG, "[%" PRIu64 "] state->result: %s set hostname null port 0 retry %d", sm_id,
                PLNHParentResultStr[state->result], state->retry);
  }

  *out_hostname     = state->hostname;
  *out_hostname_len = state->hostname_len;
  *out_port         = state->port;
  *out_retry        = state->retry;
  *out_no_cache     = state->no_cache;

  return;
}

void
PLNextHopConsistentHash::mark(TSHttpTxn txnp, void *strategyTxn, const char *hostname, const size_t hostname_len,
                              const in_port_t port, const PLNHCmd status, const time_t now)
{
  PL_NH_Debug(PL_NH_DEBUG_TAG, "mark calling");
  auto state = static_cast<PLNextHopConsistentHashTxn *>(strategyTxn);
  PLStatusTxn statusTxn;
  chTxnToStatusTxn(state, &statusTxn);
  return passive_health.mark(txnp, &statusTxn, hostname, hostname_len, port, status, now);
}
