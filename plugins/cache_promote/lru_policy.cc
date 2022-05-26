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
#include <cinttypes>

#include "lru_policy.h"

#define MINIMUM_BUCKET_SIZE 10
static LRUEntry NULL_LRU_ENTRY; // Used to create an "empty" new LRUEntry

// Initialize the LRU hash key from the TXN's URL
bool
LRUHash::initFromUrl(TSHttpTxn txnp)
{
  bool ret     = false;
  TSMLoc c_url = TS_NULL_MLOC;
  TSMBuffer reqp;
  TSMLoc req_hdr;

  if (TS_SUCCESS != TSHttpTxnClientReqGet(txnp, &reqp, &req_hdr)) {
    return false;
  }

  if (TS_SUCCESS == TSUrlCreate(reqp, &c_url)) {
    if (TS_SUCCESS == TSHttpTxnCacheLookupUrlGet(txnp, reqp, c_url)) {
      int url_len = 0;
      char *url   = TSUrlStringGet(reqp, c_url, &url_len);

      if (url && url_len > 0) {
        // SHA1() is deprecated on OpenSSL 3, but it's faster than its replacement.
#ifdef HAVE_SHA1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        SHA_CTX sha;

        SHA1_Init(&sha);
        SHA1_Update(&sha, url, url_len);
        SHA1_Final(_hash, &sha);
#pragma GCC diagnostic pop
#else
        EVP_Digest(url, url_len, _hash, nullptr, EVP_sha1(), nullptr);
#endif
        TSDebug(PLUGIN_NAME, "LRUHash::initFromUrl(%.*s%s)", url_len > 100 ? 100 : url_len, url, url_len > 100 ? "..." : "");
        TSfree(url);
        ret = true;
      }
    }
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, c_url);
  }
  TSHandleMLocRelease(reqp, TS_NULL_MLOC, req_hdr);

  return ret;
}

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
  case 'B':
    _bytes = static_cast<int64_t>(strtoll(optarg, nullptr, 10));
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
  bool ret = false;

  if (!hash.initFromUrl(txnp)) {
    return false;
  }

  // We have to hold the lock across all list and hash access / updates
  TSMutexLock(_lock);

  map_it = _map.find(&hash);
  if (_map.end() != map_it) {
    auto &[map_key, map_val]             = *map_it;
    auto &[val_key, val_hits, val_bytes] = *(map_it->second);
    bool cacheable                       = false;
    TSMBuffer request;
    TSMLoc req_hdr;

    // This is because compilers before gcc 8 aren't smart enough to ignore the unused structured bindings
    (void)val_key;

    // We check that the request is cacheable, we will still count the request, but if not cacheable, we
    // leave it in the LRU such that a subsequent request that is cacheable can properly promote.
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &request, &req_hdr)) {
      int method_len     = 0;
      const char *method = TSHttpHdrMethodGet(request, req_hdr, &method_len);

      if (TS_HTTP_METHOD_GET == method) { // Only allow GET requests (for now) to actually do the promotion
        TSMLoc range = TSMimeHdrFieldFind(request, req_hdr, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);

        if (TS_NULL_MLOC != range) { // Found a Range: header, not cacheable
          TSHandleMLocRelease(request, req_hdr, range);
        } else {
          cacheable = true;
        }
      }
      TSDebug(PLUGIN_NAME, "The request is %s", cacheable ? "cacheable" : "not cacheable");
      TSHandleMLocRelease(request, TS_NULL_MLOC, req_hdr);
    }

    // We have an entry in the LRU
    TSAssert(_list_size > 0); // mismatch in the LRUs hash and list, shouldn't happen
    incrementStat(_lru_hit_id, 1);
    ++val_hits; // Increment hits, bytes are incremented elsewhere
    if (cacheable && (val_hits >= _hits || (_bytes > 0 && val_bytes > _bytes))) {
      // Promoted! Cleanup the LRU, and signal success. Save the promoted entry on the freelist.
      TSDebug(PLUGIN_NAME, "saving the LRUEntry to the freelist");
      _freelist.splice(_freelist.begin(), _list, map_val);
      ++_freelist_size;
      --_list_size;
      _map.erase(map_key);
      incrementStat(_promoted_id, 1);
      incrementStat(_freelist_size_id, 1);
      decrementStat(_lru_size_id, 1);
      ret = true;
    } else {
      // It's still not promoted, make sure it's moved to the front of the list
      TSDebug(PLUGIN_NAME, "still not promoted, got %d hits so far and %" PRId64 " bytes", val_hits, val_bytes);
      _list.splice(_list.begin(), _list, map_val);
    }
  } else {
    // New LRU entry for the URL, try to repurpose the list entry as much as possible
    incrementStat(_lru_miss_id, 1);
    if (_list_size >= _buckets) {
      TSDebug(PLUGIN_NAME, "repurposing last LRUHash entry");
      _list.splice(_list.begin(), _list, --_list.end());
      _map.erase(&(std::get<0>(*_list.begin()))); // Get the hash from the first list element
      incrementStat(_lru_vacated_id, 1);
    } else if (_freelist_size > 0) {
      TSDebug(PLUGIN_NAME, "reusing LRUEntry from freelist");
      _list.splice(_list.begin(), _freelist, _freelist.begin());
      --_freelist_size;
      ++_list_size;
      incrementStat(_lru_size_id, 1);
      decrementStat(_freelist_size_id, 1);
    } else {
      TSDebug(PLUGIN_NAME, "creating new LRUEntry");
      _list.push_front(NULL_LRU_ENTRY);
      ++_list_size;
      incrementStat(_lru_size_id, 1);
    }
    // Update the "new" LRUEntry and add it to the hash
    *_list.begin()                       = {hash, 1, 0};
    _map[&(std::get<0>(*_list.begin()))] = _list.begin();
  }

  TSMutexUnlock(_lock);

  // If we didn't promote, and we want to count bytes, save away the calculated hash for later use
  if (false == ret && countBytes()) {
    TSUserArgSet(txnp, TXN_ARG_IDX, static_cast<void *>(new LRUHash(hash)));
  } else {
    TSUserArgSet(txnp, TXN_ARG_IDX, nullptr);
  }

  return ret;
}

void
LRUPolicy::addBytes(TSHttpTxn txnp)
{
  LRUHash *hash = static_cast<LRUHash *>(TSUserArgGet(txnp, TXN_ARG_IDX));

  if (hash) {
    LRUMap::iterator map_it;

    // We have to hold the lock across all list and hash access / updates
    TSMutexLock(_lock);
    map_it = _map.find(hash);
    if (_map.end() != map_it) {
      TSMBuffer resp;
      TSMLoc resp_hdr;

      if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &resp, &resp_hdr)) {
        TSMLoc field_loc = TSMimeHdrFieldFind(resp, resp_hdr, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);

        if (field_loc) {
          auto &[val_key, val_hits, val_bytes] = *(map_it->second);
          int64_t cl                           = TSMimeHdrFieldValueInt64Get(resp, resp_hdr, field_loc, -1);

          // This is because compilers before gcc 8 aren't smart enough to ignore the unused structured bindings
          (void)val_key, (void)val_hits;

          val_bytes += cl;
          TSDebug(PLUGIN_NAME, "Added %" PRId64 " bytes for LRU entry", cl);
          TSHandleMLocRelease(resp, resp_hdr, field_loc);
        }
        TSHandleMLocRelease(resp, TS_NULL_MLOC, resp_hdr);
      }
    }
    TSMutexUnlock(_lock);
  }
}

bool
LRUPolicy::stats_add(const char *remap_id)

{
  std::string_view remap_identifier                 = remap_id;
  const std::tuple<std::string_view, int *> stats[] = {
    {"cache_hits", &_cache_hits_id}, {"freelist_size", &_freelist_size_id},
    {"lru_size", &_lru_size_id},     {"lru_hit", &_lru_hit_id},
    {"lru_miss", &_lru_miss_id},     {"lru_vacated", &_lru_vacated_id},
    {"promoted", &_promoted_id},     {"total_requests", &_total_requests_id},
  };

  if (nullptr == remap_id) {
    TSError("[%s] no remap identifier specified for stats, no stats will be used", PLUGIN_NAME);
    return false;
  }

  for (const auto &stat : stats) {
    std::string_view name = std::get<0>(stat);
    int *id               = std::get<1>(stat);
    if ((*(id) = create_stat(name, remap_identifier)) == TS_ERROR) {
      return false;
    }
  }

  return true;
}
