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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <time.h>
#include <openssl/sha.h>

#include <string>
#include <unordered_map>
#include <list>

#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/ink_config.h"

#define MINIMUM_BUCKET_SIZE 10

static const char *PLUGIN_NAME = "cache_promote";
TSCont gNocacheCont;

//////////////////////////////////////////////////////////////////////////////////////////////
// Note that all options for all policies has to go here. Not particularly pretty...
//
static const struct option longopt[] = {{const_cast<char *>("policy"), required_argument, NULL, 'p'},
                                        // This is for both Chance and LRU (optional) policy
                                        {const_cast<char *>("sample"), required_argument, NULL, 's'},
                                        // For the LRU policy
                                        {const_cast<char *>("buckets"), required_argument, NULL, 'b'},
                                        {const_cast<char *>("hits"), required_argument, NULL, 'h'},
                                        // EOF
                                        {NULL, no_argument, NULL, '\0'}};

//////////////////////////////////////////////////////////////////////////////////////////////
// Abstract base class for all policies.
//
class PromotionPolicy
{
public:
  PromotionPolicy() : _sample(0.0)
  {
    // This doesn't have to be perfect, since this is just chance sampling.
    // coverity[dont_call]
    srand48((long)time(NULL));
  }

  void
  setSample(char *s)
  {
    _sample = strtof(s, NULL) / 100.0;
  }

  float
  getSample() const
  {
    return _sample;
  }

  bool
  doSample() const
  {
    if (_sample > 0) {
      // coverity[dont_call]
      double r = drand48();

      if (_sample > r) {
        TSDebug(PLUGIN_NAME, "checking sampling, is %f > %f? Yes!", _sample, r);
      } else {
        TSDebug(PLUGIN_NAME, "checking sampling, is %f > %f? No!", _sample, r);
        return false;
      }
    }
    return true;
  }

  virtual ~PromotionPolicy(){};

  virtual bool
  parseOption(int opt, char *optarg)
  {
    return false;
  }

  // These are pure virtual
  virtual bool doPromote(TSHttpTxn txnp) = 0;
  virtual const char *policyName() const = 0;
  virtual void usage() const             = 0;

private:
  float _sample;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// This is the simplest of all policies, just give each request a (small)
// percentage chance to be promoted to cache.
//
class ChancePolicy : public PromotionPolicy
{
public:
  bool doPromote(TSHttpTxn /* txnp ATS_UNUSED */)
  {
    TSDebug(PLUGIN_NAME, "ChancePolicy::doPromote(%f)", getSample());
    return true;
  }

  void
  usage() const
  {
    TSError("[%s] Usage: @plugin=%s.so @pparam=--policy=chance @pparam=--sample=<x>%%", PLUGIN_NAME, PLUGIN_NAME);
  }

  const char *
  policyName() const
  {
    return "chance";
  }
};

//////////////////////////////////////////////////////////////////////////////////////////////
// The LRU based policy keeps track of <bucket> number of URLs, with a counter for each slot.
// Objects are not promoted unless the counter reaches <hits> before it gets evicted. An
// optional <chance> parameter can be used to sample hits, this can reduce contention and
// churning in the LRU as well.
//
class LRUHash
{
  friend struct LRUHashHasher;

public:
  LRUHash() { TSDebug(PLUGIN_NAME, "In LRUHash()"); }
  ~LRUHash() { TSDebug(PLUGIN_NAME, "In ~LRUHash()"); }
  LRUHash &
  operator=(const LRUHash &h)
  {
    TSDebug(PLUGIN_NAME, "copying an LRUHash object");
    memcpy(_hash, h._hash, sizeof(_hash));
    return *this;
  }

  void
  init(char *data, int len)
  {
    SHA_CTX sha;

    SHA1_Init(&sha);
    SHA1_Update(&sha, data, len);
    SHA1_Final(_hash, &sha);
  }

private:
  u_char _hash[SHA_DIGEST_LENGTH];
};

struct LRUHashHasher {
  bool
  operator()(const LRUHash *s1, const LRUHash *s2) const
  {
    return 0 == memcmp(s1->_hash, s2->_hash, sizeof(s2->_hash));
  }

  size_t
  operator()(const LRUHash *s) const
  {
    return *((size_t *)s->_hash) ^ *((size_t *)(s->_hash + 9));
  }
};

typedef std::pair<LRUHash, unsigned> LRUEntry;
typedef std::list<LRUEntry> LRUList;
typedef std::unordered_map<const LRUHash *, LRUList::iterator, LRUHashHasher, LRUHashHasher> LRUMap;

static LRUEntry NULL_LRU_ENTRY; // Used to create an "empty" new LRUEntry

class LRUPolicy : public PromotionPolicy
{
public:
  LRUPolicy() : PromotionPolicy(), _buckets(1000), _hits(10), _lock(TSMutexCreate()) {}
  ~LRUPolicy()
  {
    TSDebug(PLUGIN_NAME, "deleting LRUPolicy object");
    TSMutexLock(_lock);

    _map.clear();
    _list.clear();
    _freelist.clear();

    TSMutexUnlock(_lock);
    TSMutexDestroy(_lock);
  }

  bool
  parseOption(int opt, char *optarg)
  {
    switch (opt) {
    case 'b':
      _buckets = static_cast<unsigned>(strtol(optarg, NULL, 10));
      if (_buckets < MINIMUM_BUCKET_SIZE) {
        TSError("%s: Enforcing minimum LRU bucket size of %d", PLUGIN_NAME, MINIMUM_BUCKET_SIZE);
        TSDebug(PLUGIN_NAME, "Enforcing minimum bucket size of %d", MINIMUM_BUCKET_SIZE);
        _buckets = MINIMUM_BUCKET_SIZE;
      }
      break;
    case 'h':
      _hits = static_cast<unsigned>(strtol(optarg, NULL, 10));
      break;
    default:
      // All other options are unsupported for this policy
      return false;
    }

    // This doesn't have to be perfect, since this is just chance sampling.
    // coverity[dont_call]
    srand48((long)time(NULL) ^ (long)getpid() ^ (long)getppid());

    return true;
  }

  bool
  doPromote(TSHttpTxn txnp)
  {
    LRUHash hash;
    LRUMap::iterator map_it;
    char *url   = NULL;
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
      TSAssert(_list.size() > 0); // mismatch in the LRUs hash and list, shouldn't happen
      if (++(map_it->second->second) >= _hits) {
        // Promoted! Cleanup the LRU, and signal success. Save the promoted entry on the freelist.
        TSDebug(PLUGIN_NAME, "saving the LRUEntry to the freelist");
        _freelist.splice(_freelist.begin(), _list, map_it->second);
        _map.erase(map_it->first);
        ret = true;
      } else {
        // It's still not promoted, make sure it's moved to the front of the list
        TSDebug(PLUGIN_NAME, "still not promoted, got %d hits so far", map_it->second->second);
        _list.splice(_list.begin(), _list, map_it->second);
      }
    } else {
      // New LRU entry for the URL, try to repurpose the list entry as much as possible
      if (_list.size() >= _buckets) {
        TSDebug(PLUGIN_NAME, "repurposing last LRUHash entry");
        _list.splice(_list.begin(), _list, --_list.end());
        _map.erase(&(_list.begin()->first));
      } else if (_freelist.size() > 0) {
        TSDebug(PLUGIN_NAME, "reusing LRUEntry from freelist");
        _list.splice(_list.begin(), _freelist, _freelist.begin());
      } else {
        TSDebug(PLUGIN_NAME, "creating new LRUEntry");
        _list.push_front(NULL_LRU_ENTRY);
      }
      // Update the "new" LRUEntry and add it to the hash
      _list.begin()->first          = hash;
      _list.begin()->second         = 1;
      _map[&(_list.begin()->first)] = _list.begin();
    }

    TSMutexUnlock(_lock);

    return ret;
  }

  void
  usage() const
  {
    TSError("[%s] Usage: @plugin=%s.so @pparam=--policy=lru @pparam=--buckets=<n> --hits=<m> --sample=<x>", PLUGIN_NAME,
            PLUGIN_NAME);
  }

  const char *
  policyName() const
  {
    return "LRU";
  }

private:
  unsigned _buckets;
  unsigned _hits;
  // For the LRU
  TSMutex _lock;
  LRUMap _map;
  LRUList _list, _freelist;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// This holds the configuration for a remap rule, as well as parses the configurations.
//
class PromotionConfig
{
public:
  PromotionConfig() : _policy(NULL) {}
  ~PromotionConfig() { delete _policy; }
  PromotionPolicy *
  getPolicy() const
  {
    return _policy;
  }

  // Parse the command line arguments to the plugin, and instantiate the appropriate policy
  bool
  factory(int argc, char *argv[])
  {
    while (true) {
      int opt = getopt_long(argc, (char *const *)argv, "psbh", longopt, NULL);

      if (opt == -1) {
        break;
      } else if (opt == 'p') {
        if (0 == strncasecmp(optarg, "chance", 6)) {
          _policy = new ChancePolicy();
        } else if (0 == strncasecmp(optarg, "lru", 3)) {
          _policy = new LRUPolicy();
        } else {
          TSError("[%s] Unknown policy --policy=%s", PLUGIN_NAME, optarg);
          return false;
        }
        if (_policy) {
          TSDebug(PLUGIN_NAME, "created remap with cache promotion policy = %s", _policy->policyName());
        }
      } else {
        if (_policy) {
          // The --sample (-s) option is allowed for all configs, but only after --policy is specified.
          if (opt == 's') {
            _policy->setSample(optarg);
          } else {
            if (!_policy->parseOption(opt, optarg)) {
              TSError("[%s] The specified policy (%s) does not support the -%c option", PLUGIN_NAME, _policy->policyName(), opt);
              delete _policy;
              _policy = NULL;
              return false;
            }
          }
        } else {
          TSError("[%s] The --policy=<n> parameter must come first on the remap configuration", PLUGIN_NAME);
          return false;
        }
      }
    }

    return true;
  }

private:
  PromotionPolicy *_policy;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Little helper continuation, to turn off writing to the cache. ToDo: when we have proper
// APIs to make requests / responses, we can remove this completely.
static int
cont_nocache_response(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  TSHttpTxnServerRespNoStoreSet(txnp, 1);
  // Reenable and continue with the state machine.
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Main "plugin", a TXN hook in the TS_HTTP_READ_CACHE_HDR_HOOK. Unless the policy allows
// caching, we will turn off the cache from here on for the TXN.
//
// NOTE: This is not optimal, the goal was to handle this before we lock the URL in the
// cache. However, that does not work. Hence, for now, we also schedule the continuation
// for READ_RESPONSE_HDR such that we can turn off  the actual cache write.
//
static int
cont_handle_policy(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp          = static_cast<TSHttpTxn>(edata);
  PromotionConfig *config = static_cast<PromotionConfig *>(TSContDataGet(contp));

  switch (event) {
  // Main HOOK
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    if (TS_SUCCESS != TSHttpTxnIsInternal(txnp)) {
      int obj_status;

      if (TS_ERROR != TSHttpTxnCacheLookupStatusGet(txnp, &obj_status)) {
        switch (obj_status) {
        case TS_CACHE_LOOKUP_MISS:
        case TS_CACHE_LOOKUP_SKIPPED:
          if (config->getPolicy()->doSample() && config->getPolicy()->doPromote(txnp)) {
            TSDebug(PLUGIN_NAME, "cache-status is %d, and leaving cache on (promoted)", obj_status);
          } else {
            TSDebug(PLUGIN_NAME, "cache-status is %d, and turning off the cache (not promoted)", obj_status);
            TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, gNocacheCont);
          }
          break;
        default:
          // Do nothing, just let it handle the lookup.
          TSDebug(PLUGIN_NAME, "cache-status is %d (hit), nothing to do", obj_status);
          break;
        }
      }
    } else {
      TSDebug(PLUGIN_NAME, "Request is an internal (plugin) request, implicitly promoted");
    }
    break;

  // Should not happen
  default:
    TSDebug(PLUGIN_NAME, "Unhandled event %d", (int)event);
    break;
  }

  // Reenable and continue with the state machine.
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[tsremap_init] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[tsremap_init] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  gNocacheCont = TSContCreate(cont_nocache_response, NULL);

  TSDebug(PLUGIN_NAME, "remap plugin is successfully initialized");
  return TS_SUCCESS; /* success */
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  PromotionConfig *config = new PromotionConfig;

  --argc;
  ++argv;
  if (config->factory(argc, argv)) {
    TSCont contp = TSContCreate(cont_handle_policy, NULL);

    TSContDataSet(contp, static_cast<void *>(config));
    *ih = static_cast<void *>(contp);

    return TS_SUCCESS;
  } else {
    delete config;
    return TS_ERROR;
  }
}

void
TSRemapDeleteInstance(void *ih)
{
  TSCont contp            = static_cast<TSCont>(ih);
  PromotionConfig *config = static_cast<PromotionConfig *>(TSContDataGet(contp));

  delete config;
  TSContDestroy(contp);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Schedule the cache-read continuation for this remap rule.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo * /* ATS_UNUSED rri */)
{
  if (NULL == ih) {
    TSDebug(PLUGIN_NAME, "No promotion rules configured, this is probably a plugin bug");
  } else {
    TSCont contp = static_cast<TSCont>(ih);

    TSDebug(PLUGIN_NAME, "scheduling a TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK hook");
    TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
  }

  return TSREMAP_NO_REMAP;
}
