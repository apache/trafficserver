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

/**
 * @file bg_fetch.h
 * @brief Background fetch related classes (header file).
 */

#pragma once

#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "ts/ts.h"
#include "ts/experimental.h"
#include "common.h"
#include "configs.h"
#include "fetch_policy.h"

enum PrefetchMetric {
  FETCH_ACTIVE = 0,
  FETCH_COMPLETED,
  FETCH_ERRORS,
  FETCH_TIMEOOUTS,
  FETCH_THROTTLED,
  FETCH_ALREADY_CACHED, /*metric if for counting how many times fetch was not scheduled because of cache-hit */
  FETCH_TOTAL,
  FETCH_UNIQUE_YES,
  FETCH_UNIQUE_NO,
  FETCH_MATCH_YES,  /* metric id for URL path pattern match successes */
  FETCH_MATCH_NO,   /* metric id for URL path pattern match failures */
  FETCH_POLICY_YES, /* metric id for counting fetch policy successes */
  FETCH_POLICY_NO,  /* metric id for counting fetch policy failures */
  FETCH_POLICY_SIZE,
  FETCH_POLICY_MAXSIZE,
  FETCHES_MAX_METRICS,
};

struct PrefetchMetricInfo {
  PrefetchMetric index;
  TSRecordDataType type;
  int id;
};

/**
 * @brief to store background fetch state, metrics, logs etc (shared between all scheduled fetches).
 *
 * @todo: reconsider the locks (tried to be granular but it feels too crowded, remove unnecessary locks)
 */
class BgFetchState
{
public:
  BgFetchState();
  virtual ~BgFetchState();
  bool init(const PrefetchConfig &config);

  /* Fetch policy */
  bool acquire(const String &url);
  bool release(const String &url);

  /* De-duplication of requests */
  bool uniqueAcquire(const String &url);
  bool uniqueRelease(const String &url);

  /* Metrics and logs */
  void incrementMetric(PrefetchMetric m);
  void setMetric(PrefetchMetric m, size_t value);
  TSTextLogObject getLog();

private:
  BgFetchState(BgFetchState const &);   /* never implement */
  void operator=(BgFetchState const &); /* never implement */

  /* Fetch policy related */
  FetchPolicy *_policy = nullptr; /* fetch policy */
  TSMutex _policyLock;            /* protects the policy object only */

  /* Mechanisms to avoid concurrent fetches and applying limits */
  FetchPolicy *_unique = nullptr; /* make sure we never download same object multiple times at the same time */
  TSMutex _lock;                  /* protects the deduplication object only */
  size_t _concurrentFetches                        = 0;
  size_t _concurrentFetchesMax                     = 0;
  PrefetchMetricInfo _metrics[FETCHES_MAX_METRICS] = {
    {FETCH_ACTIVE, TS_RECORDDATATYPE_INT, -1},        {FETCH_COMPLETED, TS_RECORDDATATYPE_COUNTER, -1},
    {FETCH_ERRORS, TS_RECORDDATATYPE_COUNTER, -1},    {FETCH_TIMEOOUTS, TS_RECORDDATATYPE_COUNTER, -1},
    {FETCH_THROTTLED, TS_RECORDDATATYPE_COUNTER, -1}, {FETCH_ALREADY_CACHED, TS_RECORDDATATYPE_COUNTER, -1},
    {FETCH_TOTAL, TS_RECORDDATATYPE_COUNTER, -1},     {FETCH_UNIQUE_YES, TS_RECORDDATATYPE_COUNTER, -1},
    {FETCH_UNIQUE_NO, TS_RECORDDATATYPE_COUNTER, -1}, {FETCH_MATCH_YES, TS_RECORDDATATYPE_COUNTER, -1},
    {FETCH_MATCH_NO, TS_RECORDDATATYPE_COUNTER, -1},  {FETCH_POLICY_YES, TS_RECORDDATATYPE_COUNTER, -1},
    {FETCH_POLICY_NO, TS_RECORDDATATYPE_COUNTER, -1}, {FETCH_POLICY_SIZE, TS_RECORDDATATYPE_INT, -1},
    {FETCH_POLICY_MAXSIZE, TS_RECORDDATATYPE_INT, -1}};

  /* plugin specific fetch logging */
  TSTextLogObject _log = nullptr;
};

/**
 * @brief Contains all background states to be shared between different plugin instances (grouped in namespaces)
 */
class BgFetchStates
{
public:
  /* Initialize on first use */
  static BgFetchStates *
  get()
  {
    if (nullptr == _prefetchStates) {
      _prefetchStates = new BgFetchStates();
    }
    return _prefetchStates;
  }

  BgFetchState *
  getStateByName(const String &space)
  {
    BgFetchState *state;
    std::map<String, BgFetchState *>::iterator it;

    TSMutexLock(_prefetchStates->_lock);
    it = _prefetchStates->_states.find(space);
    if (it != _prefetchStates->_states.end()) {
      state = it->second;
    } else {
      state                           = new BgFetchState();
      _prefetchStates->_states[space] = state;
    }
    TSMutexUnlock(_prefetchStates->_lock);
    return state;
  }

private:
  BgFetchStates() : _lock(TSMutexCreate()) {}
  ~BgFetchStates() { TSMutexDestroy(_lock); }
  static BgFetchStates *_prefetchStates;

  std::map<String, BgFetchState *> _states; /* stores pointers to states per namespace */
  TSMutex _lock;
};

/**
 * @brief Represents a single background fetch.
 */
class BgFetch
{
public:
  static bool schedule(BgFetchState *state, const PrefetchConfig &config, bool askPermission, TSMBuffer requestBuffer,
                       TSMLoc requestHeaderLoc, TSHttpTxn txnp, const char *path, size_t pathLen, const String &cachekey);

private:
  BgFetch(BgFetchState *state, const PrefetchConfig &config, bool lock);
  ~BgFetch();
  bool init(TSMBuffer requestBuffer, TSMLoc requestHeaderLoc, TSHttpTxn txnp, const char *fetchPath, size_t fetchPathLen,
            const String &cacheKey);
  void schedule();
  static int handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */);
  bool saveIp(TSHttpTxn txnp);
  void addBytes(int64_t b);
  void logAndMetricUpdate(TSEvent event) const;

  /* Request related */
  TSMBuffer _mbuf;
  TSMLoc _headerLoc;
  TSMLoc _urlLoc;
  struct sockaddr_storage client_ip;

  /* This is for the actual background fetch / NetVC */
  TSVConn vc;
  TSIOBuffer req_io_buf, resp_io_buf;
  TSIOBufferReader req_io_buf_reader, resp_io_buf_reader;
  TSVIO r_vio, w_vio;
  int64_t _bytes;

  /* Background fetch continuation */
  TSCont _cont;

  /* Pointers and cache */
  String _cachekey;              /* saving the cache key for later use */
  String _url;                   /* saving the URL for later use */
  BgFetchState *_state;          /* pointer for access to the plugin state */
  const PrefetchConfig &_config; /* reference for access to the configuration */

  bool _askPermission; /* true - check with the fetch policies if we should schedule the fetch */

  TSHRTime _startTime; /* for calculation of downloadTime for this fetch */
};
