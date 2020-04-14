/*
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
#include <unistd.h>

#include "lru_policy.h"

#define MINIMUM_BUCKET_SIZE 10
static LRUEntry NULL_LRU_ENTRY; // Used to create an "empty" new LRUEntry

LRUPolicy::~LRUPolicy()
{
  TSDebug(PLUGIN_NAME, "LRUPolicy DTOR");
  TSMutexLock(_lock);

  _map.clear();
  _list.clear();
  _list_size = 0;
  _freelist.clear();
  _freelist_size = 0;

  TSMutexUnlock(_lock);
  TSMutexDestroy(_lock);
}

bool
LRUPolicy::parseOption(int opt, char *optarg)
{
  switch (opt) {
  case 'b':
    _buckets = static_cast<unsigned>(strtol(optarg, nullptr, 10));
    if (_buckets < MINIMUM_BUCKET_SIZE) {
      TSError("%s: Enforcing minimum LRU bucket size of %d", PLUGIN_NAME, MINIMUM_BUCKET_SIZE);
      TSDebug(PLUGIN_NAME, "enforcing minimum bucket size of %d", MINIMUM_BUCKET_SIZE);
      _buckets = MINIMUM_BUCKET_SIZE;
    }
    break;
  case 'h':
    _hits = static_cast<unsigned>(strtol(optarg, nullptr, 10));
    break;
  case 'l':
    _label = optarg;
    break;
  default:
    // All other options are unsupported for this policy
    return false;
  }

  // This doesn't have to be perfect, since this is just chance sampling.
  // coverity[dont_call]
  srand48(static_cast<long>(time(nullptr)) ^ static_cast<long>(getpid()) ^ static_cast<long>(getppid()));

  return true;
}

bool
LRUPolicy::doPromote(TSHttpTxn txnp)
{
  LRUHash hash;
  LRUMap::iterator map_it;
  char *url   = nullptr;
  int url_len = 0;
  bool ret    = false;
  TSMBuffer request;
  TSMLoc req_hdr;

  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &request, &req_hdr)) {
    TSMLoc c_url = TS_NULL_MLOC;

    // Get the cache key URL (for now), since this has better lookup behavior when using
    // e.g. the cachekey plugin.
    if (TS_SUCCESS == TSUrlCreate(request, &c_url)) {
      if (TS_SUCCESS == TSHttpTxnCacheLookupUrlGet(txnp, request, c_url)) {
        url = TSUrlStringGet(request, c_url, &url_len);
        TSHandleMLocRelease(request, TS_NULL_MLOC, c_url);
      }
    }
    TSHandleMLocRelease(request, TS_NULL_MLOC, req_hdr);
  }

  // Generally shouldn't happen ...
  if (!url) {
    return false;
  }

  TSDebug(PLUGIN_NAME, "LRUPolicy::doPromote(%.*s%s)", url_len > 100 ? 100 : url_len, url, url_len > 100 ? "..." : "");
  hash.init(url, url_len);
  TSfree(url);

  // We have to hold the lock across all list and hash access / updates
  TSMutexLock(_lock);

  map_it = _map.find(&hash);
  if (_map.end() != map_it) {
    // We have an entry in the LRU
    TSAssert(_list_size > 0); // mismatch in the LRUs hash and list, shouldn't happen
    incrementStat(lru_hit_id, 1);
    if (++(map_it->second->second) >= _hits) {
      // Promoted! Cleanup the LRU, and signal success. Save the promoted entry on the freelist.
      TSDebug(PLUGIN_NAME, "saving the LRUEntry to the freelist");
      _freelist.splice(_freelist.begin(), _list, map_it->second);
      ++_freelist_size;
      --_list_size;
      _map.erase(map_it->first);
      incrementStat(promoted_id, 1);
      incrementStat(freelist_size_id, 1);
      decrementStat(lru_size_id, 1);
      ret = true;
    } else {
      // It's still not promoted, make sure it's moved to the front of the list
      TSDebug(PLUGIN_NAME, "still not promoted, got %d hits so far", map_it->second->second);
      _list.splice(_list.begin(), _list, map_it->second);
    }
  } else {
    // New LRU entry for the URL, try to repurpose the list entry as much as possible
    incrementStat(lru_miss_id, 1);
    if (_list_size >= _buckets) {
      TSDebug(PLUGIN_NAME, "repurposing last LRUHash entry");
      _list.splice(_list.begin(), _list, --_list.end());
      _map.erase(&(_list.begin()->first));
      incrementStat(lru_vacated_id, 1);
    } else if (_freelist_size > 0) {
      TSDebug(PLUGIN_NAME, "reusing LRUEntry from freelist");
      _list.splice(_list.begin(), _freelist, _freelist.begin());
      --_freelist_size;
      ++_list_size;
      incrementStat(lru_size_id, 1);
      decrementStat(freelist_size_id, 1);
    } else {
      TSDebug(PLUGIN_NAME, "creating new LRUEntry");
      _list.push_front(NULL_LRU_ENTRY);
      ++_list_size;
      incrementStat(lru_size_id, 1);
    }
    // Update the "new" LRUEntry and add it to the hash
    _list.begin()->first          = hash;
    _list.begin()->second         = 1;
    _map[&(_list.begin()->first)] = _list.begin();
  }

  TSMutexUnlock(_lock);

  return ret;
}

bool
LRUPolicy::stats_add(const char *remap_id)

{
  std::string_view remap_identifier                 = remap_id;
  const std::tuple<std::string_view, int *> stats[] = {
    {"cache_hits", &cache_hits_id}, {"freelist_size", &freelist_size_id},
    {"lru_size", &lru_size_id},     {"lru_hit", &lru_hit_id},
    {"lru_miss", &lru_miss_id},     {"lru_vacated", &lru_vacated_id},
    {"promoted", &promoted_id},     {"total_requests", &total_requests_id},
  };

  if (nullptr == remap_id) {
    TSError("[%s] no remap identifier specified for for stats, no stats will be used", PLUGIN_NAME);
    return false;
  }

  for (int ii = 0; ii < 8; ii++) {
    std::string_view name = std::get<0>(stats[ii]);
    int *id               = std::get<1>(stats[ii]);
    if ((*(id) = create_stat(name, remap_identifier)) == TS_ERROR) {
      return false;
    }
  }

  return true;
}
