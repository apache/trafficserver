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
 * @file bg_fetch.cpp
 * @brief Background fetch related classes (header file).
 */

#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <sys/socket.h>
#include <inttypes.h>

#include "ts/ts.h" /* ATS API */
#include "fetch.h"
#include "headers.h"

const char *
getPrefetchMetricsNames(int metric)
{
  switch (metric) {
  case FETCH_ACTIVE:
    return "fetch.active";
    break;
  case FETCH_COMPLETED:
    return "fetch.completed";
    break;
  case FETCH_ERRORS:
    return "fetch.errors";
    break;
  case FETCH_TIMEOOUTS:
    return "fetch.timeouts";
    break;
  case FETCH_THROTTLED:
    return "fetch.throttled";
    break;
  case FETCH_ALREADY_CACHED:
    return "fetch.already_cached";
    break;
  case FETCH_TOTAL:
    return "fetch.total";
    break;
  case FETCH_UNIQUE_YES:
    return "fetch.unique.yes";
    break;
  case FETCH_UNIQUE_NO:
    return "fetch.unique.no";
    break;
  case FETCH_MATCH_YES:
    return "fetch.match.yes";
    break;
  case FETCH_MATCH_NO:
    return "fetch.match.no";
    break;
  case FETCH_POLICY_YES:
    return "fetch.policy.yes";
    break;
  case FETCH_POLICY_NO:
    return "fetch.policy.no";
    break;
  case FETCH_POLICY_SIZE:
    return "fetch.policy.size";
    break;
  case FETCH_POLICY_MAXSIZE:
    return "fetch.policy.maxsize";
    break;
  default:
    return "unknown";
    break;
  }
}

static bool
createStat(const String &prefix, const String &space, const char *module, const char *statName, TSRecordDataType statType,
           int &statId)
{
  String name(prefix);
  name.append(".").append(space);
  if (nullptr != module) {
    name.append(".").append(module);
  }
  name.append(".").append(statName);

  if (TSStatFindName(name.c_str(), &statId) == TS_ERROR) {
    statId = TSStatCreate(name.c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    if (statId == TS_ERROR) {
      PrefetchError("failed to register '%s'", name.c_str());
      return false;
    }

    TSStatIntSet(statId, 0);
  }

  PrefetchDebug("created metric '%s (id:%d)'", name.c_str(), statId);

  return true;
}

BgFetchState::BgFetchState() : _policy(nullptr), _unique(nullptr), _concurrentFetches(0), _concurrentFetchesMax(0), _log(nullptr)
{
  _policyLock = TSMutexCreate();
  if (nullptr == _policyLock) {
    PrefetchError("failed to initialize lock");
  } else {
    PrefetchDebug("initialized lock");
  }

  _lock = TSMutexCreate();
  if (nullptr == _lock) {
    PrefetchError("failed to initialize lock");
  } else {
    PrefetchDebug("initialized lock");
  }
}

BgFetchState::~BgFetchState()
{
  TSMutexLock(_policyLock);
  delete _policy;
  TSMutexUnlock(_policyLock);

  TSMutexLock(_lock);
  delete _unique;
  TSMutexUnlock(_lock);

  TSMutexDestroy(_policyLock);
  TSMutexDestroy(_lock);

  TSTextLogObjectFlush(_log);
  TSTextLogObjectDestroy(_log);
}

static bool
initializePolicy(FetchPolicy *&policy, const char *policyName)
{
  bool status = true;
  if (nullptr == policy) {
    policy = FetchPolicy::getInstance(policyName);
    if (nullptr == policy) {
      PrefetchError("failed to initialize the %s policy", policyName);
      status = false;
    }
  } else {
    PrefetchDebug("state already initialized");
  }
  return status;
}

bool
initializeMetrics(PrefetchMetricInfo metrics[], const PrefetchConfig &config)
{
  bool status = true;
  for (int i = FETCH_ACTIVE; i < FETCHES_MAX_METRICS; i++) {
    if (-1 == metrics[i].id) {
      status = createStat(config.getMetricsPrefix(), config.getNameSpace(), nullptr, getPrefetchMetricsNames(i), metrics[i].type,
                          metrics[i].id);
    } else {
      PrefetchDebug("metric %s already initialized", getPrefetchMetricsNames(i));
    }
  }
  return status;
}

bool
initializeLog(TSTextLogObject &log, const PrefetchConfig &config)
{
  bool status = true;
  if (!config.getLogName().empty()) {
    if (nullptr == log) {
      TSReturnCode error = TSTextLogObjectCreate(config.getLogName().c_str(), TS_LOG_MODE_ADD_TIMESTAMP, &log);
      if (error != TS_SUCCESS) {
        PrefetchError("failed to create log file");
        status = false;
      } else {
        PrefetchDebug("initialized log file '%s'", config.getLogName().c_str());
      }
    } else {
      PrefetchDebug("log file '%s' already initialized", config.getLogName().c_str());
    }
  } else {
    PrefetchDebug("skip creating log file");
  }
  return status;
}

bool
BgFetchState::init(const PrefetchConfig &config)
{
  int status = true;

  /* Is throttling configured, 0 - don't throttle */
  _concurrentFetchesMax = config.getFetchMax();

  /* Initialize the state */
  TSMutexLock(_lock);

  /* Initialize 'simple' policy used to avoid concurrent fetches of the same object */
  status &= initializePolicy(_unique, "simple");

  /* Initialize the fetch metrics */
  status &= initializeMetrics(_metrics, config);

  /* Initialize the "pre-fetch" log */
  status &= initializeLog(_log, config);

  TSMutexUnlock(_lock);

  /* Initialize fetching policy */
  TSMutexLock(_policyLock);

  if (!config.getFetchPolicy().empty() && 0 != config.getFetchPolicy().compare("simple")) {
    status &= initializePolicy(_policy, config.getFetchPolicy().c_str());
    if (nullptr != _policy) {
      setMetric(FETCH_POLICY_MAXSIZE, _policy->getMaxSize());
    }
  } else {
    PrefetchDebug("Policy not specified or 'simple' policy chosen (skipping)");
  }

  TSMutexUnlock(_policyLock);

  return status;
}

bool
BgFetchState::acquire(const String &url)
{
  bool permitted = true;
  if (nullptr != _policy) {
    TSMutexLock(_policyLock);
    permitted = _policy->acquire(url);
    TSMutexUnlock(_policyLock);
  }

  if (permitted) {
    incrementMetric(FETCH_POLICY_YES);
  } else {
    incrementMetric(FETCH_POLICY_NO);
  }

  if (nullptr != _policy) {
    setMetric(FETCH_POLICY_SIZE, _policy->getSize());
  }

  return permitted;
}

bool
BgFetchState::release(const String &url)
{
  bool ret = true;
  if (nullptr != _policy) {
    TSMutexLock(_policyLock);
    ret &= _policy->release(url);
    TSMutexUnlock(_policyLock);
  }

  if (nullptr != _policy) {
    setMetric(FETCH_POLICY_SIZE, _policy->getSize());
  }

  return ret;
}

bool
BgFetchState::uniqueAcquire(const String &url)
{
  bool permitted       = true;
  bool throttled       = false;
  size_t cachedCounter = 0;

  TSMutexLock(_lock);
  if (0 == _concurrentFetchesMax || _concurrentFetches < _concurrentFetchesMax) {
    permitted = _unique->acquire(url);
    if (permitted) {
      cachedCounter = ++_concurrentFetches;
    }
  } else {
    throttled = true;
  }
  TSMutexUnlock(_lock);

  /* Update the metrics, no need to lock? */
  if (throttled) {
    incrementMetric(FETCH_THROTTLED);
  }

  if (permitted && !throttled) {
    incrementMetric(FETCH_UNIQUE_YES);
    incrementMetric(FETCH_TOTAL);
    setMetric(FETCH_ACTIVE, cachedCounter);
  } else {
    incrementMetric(FETCH_UNIQUE_NO);
  }

  return permitted;
}

bool
BgFetchState::uniqueRelease(const String &url)
{
  bool permitted        = true;
  ssize_t cachedCounter = 0;

  TSMutexLock(_lock);
  cachedCounter = --_concurrentFetches;
  permitted     = _unique->release(url);
  TSMutexUnlock(_lock);

  TSAssert(cachedCounter < 0);

  /* Update the metrics, no need to lock? */
  if (permitted) {
    setMetric(FETCH_ACTIVE, cachedCounter);
  }
  return permitted;
}

void
BgFetchState::incrementMetric(PrefetchMetric m)
{
  if (-1 != _metrics[m].id) {
    TSStatIntIncrement(_metrics[m].id, 1);
  }
}

void
BgFetchState::setMetric(PrefetchMetric m, size_t value)
{
  if (-1 != _metrics[m].id) {
    TSStatIntSet(_metrics[m].id, value);
  }
}

inline TSTextLogObject
BgFetchState::getLog()
{
  return _log;
}
BgFetchStates *BgFetchStates::_prefetchStates = nullptr;

BgFetch::BgFetch(BgFetchState *state, const PrefetchConfig &config, bool lock)
  : _headerLoc(TS_NULL_MLOC),
    _urlLoc(TS_NULL_MLOC),
    vc(nullptr),
    req_io_buf(nullptr),
    resp_io_buf(nullptr),
    req_io_buf_reader(nullptr),
    resp_io_buf_reader(nullptr),
    r_vio(nullptr),
    w_vio(nullptr),
    _bytes(0),
    _cont(nullptr),
    _state(state),
    _config(config),
    _askPermission(lock),
    _startTime(0)
{
  _mbuf = TSMBufferCreate();
  memset(&client_ip, 0, sizeof(client_ip));
}

BgFetch::~BgFetch()
{
  TSHandleMLocRelease(_mbuf, TS_NULL_MLOC, _headerLoc);
  TSHandleMLocRelease(_mbuf, TS_NULL_MLOC, _urlLoc);

  TSMBufferDestroy(_mbuf);

  if (vc) {
    PrefetchError("Destroyed BgFetch while VC was alive");
    TSVConnClose(vc);
    vc = nullptr;
  }

  if (nullptr != _cont) {
    if (_askPermission) {
      _state->release(_cachekey);
      _state->uniqueRelease(_cachekey);
    }

    TSContDestroy(_cont);
    _cont = nullptr;

    TSIOBufferReaderFree(req_io_buf_reader);
    TSIOBufferDestroy(req_io_buf);
    TSIOBufferReaderFree(resp_io_buf_reader);
    TSIOBufferDestroy(resp_io_buf);
  }
}

bool
BgFetch::schedule(BgFetchState *state, const PrefetchConfig &config, bool askPermission, TSMBuffer requestBuffer,
                  TSMLoc requestHeaderLoc, TSHttpTxn txnp, const char *path, size_t pathLen, const String &cachekey)
{
  bool ret       = false;
  BgFetch *fetch = new BgFetch(state, config, askPermission);
  if (fetch->init(requestBuffer, requestHeaderLoc, txnp, path, pathLen, cachekey)) {
    fetch->schedule();
    ret = true;
  } else {
    delete fetch;
  }
  return ret;
}

bool
BgFetch::saveIp(TSHttpTxn txnp)
{
  struct sockaddr const *ip = TSHttpTxnClientAddrGet(txnp);
  if (ip) {
    if (ip->sa_family == AF_INET) {
      memcpy(&client_ip, ip, sizeof(sockaddr_in));
    } else if (ip->sa_family == AF_INET6) {
      memcpy(&client_ip, ip, sizeof(sockaddr_in6));
    } else {
      PrefetchError("unknown address family %d", ip->sa_family);
    }
  } else {
    PrefetchError("failed to get client host info");
    return false;
  }
  return true;
}

inline void
BgFetch::addBytes(int64_t b)
{
  _bytes += b;
}
/**
 * Initialize the background fetch
 */
bool
BgFetch::init(TSMBuffer reqBuffer, TSMLoc reqHdrLoc, TSHttpTxn txnp, const char *fetchPath, size_t fetchPathLen,
              const String &cachekey)
{
  TSAssert(TS_NULL_MLOC == _headerLoc);
  TSAssert(TS_NULL_MLOC == _urlLoc);

  if (_askPermission) {
    if (!_state->acquire(cachekey)) {
      PrefetchDebug("request is not fetchable");
      return false;
    }

    if (!_state->uniqueAcquire(cachekey)) {
      PrefetchDebug("already fetching the object");
      _state->release(cachekey);
      return false;
    }
  }

  _cachekey.assign(cachekey);

  /* Save the IP info */
  if (!saveIp(txnp)) {
    return false;
  }

  /* Create HTTP header */
  _headerLoc = TSHttpHdrCreate(_mbuf);

  /* Copy the headers to the new marshal buffer */
  if (TS_SUCCESS != TSHttpHdrCopy(_mbuf, _headerLoc, reqBuffer, reqHdrLoc)) {
    PrefetchError("header copy failed");
  }

  /* Copy the pristine request URL into fetch marshal buffer */
  TSMLoc pristineUrlLoc;
  if (TS_SUCCESS == TSHttpTxnPristineUrlGet(txnp, &reqBuffer, &pristineUrlLoc)) {
    if (TS_SUCCESS != TSUrlClone(_mbuf, reqBuffer, pristineUrlLoc, &_urlLoc)) {
      PrefetchError("failed to clone URL");
      TSHandleMLocRelease(reqBuffer, TS_NULL_MLOC, pristineUrlLoc);
      return false;
    }
    TSHandleMLocRelease(reqBuffer, TS_NULL_MLOC, pristineUrlLoc);
  } else {
    PrefetchError("failed to get pristine URL");
    return false;
  }

  /* Save the path before changing */
  int pathLen;
  const char *path = TSUrlPathGet(_mbuf, _urlLoc, &pathLen);
  if (nullptr == path) {
    PrefetchError("failed to get a URL path");
    return false;
  }

  /* Now set or remove the prefetch API header */
  const String &header = _config.getApiHeader();
  if (_config.isFront()) {
    if (setHeader(_mbuf, _headerLoc, header.c_str(), (int)header.length(), path, pathLen)) {
      PrefetchDebug("set header '%.*s: %.*s'", (int)header.length(), header.c_str(), (int)fetchPathLen, fetchPath);
    }
  } else {
    if (removeHeader(_mbuf, _headerLoc, header.c_str(), header.length())) {
      PrefetchDebug("remove header '%.*s'", (int)header.length(), header.c_str());
    }
  }

  /* Make sure we remove the RANGE header to avoid 416 "Request Range Not Satisfiable" response when
   * the current request is a RANGE request and its range turns out invalid for the "next" object */
  if (removeHeader(_mbuf, _headerLoc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE)) {
    PrefetchDebug("remove header '%.*s'", TS_MIME_LEN_RANGE, TS_MIME_FIELD_RANGE);
  }

  /* Overwrite the path if required */
  if (nullptr != fetchPath && 0 != fetchPathLen) {
    if (TS_SUCCESS == TSUrlPathSet(_mbuf, _urlLoc, fetchPath, fetchPathLen)) {
      PrefetchDebug("setting URL path to %.*s", (int)fetchPathLen, fetchPath);
    } else {
      PrefetchError("failed to set a URL path %.*s", (int)fetchPathLen, fetchPath);
    }
  }

  /* Come up with the host name to be used in the fetch request */
  const char *hostName = nullptr;
  int hostNameLen      = 0;
  if (_config.getReplaceHost().empty()) {
    hostName = TSUrlHostGet(_mbuf, _urlLoc, &hostNameLen);
  } else {
    hostName    = _config.getReplaceHost().c_str();
    hostNameLen = _config.getReplaceHost().length();
  }

  /* Set the URI host */
  if (TS_SUCCESS == TSUrlHostSet(_mbuf, _urlLoc, hostName, hostNameLen)) {
    PrefetchDebug("setting URL host: %.*s", hostNameLen, hostName);
  } else {
    PrefetchError("failed to set URL host: %.*s", hostNameLen, hostName);
  }

  /* Set the host header */
  if (setHeader(_mbuf, _headerLoc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST, hostName, hostNameLen)) {
    PrefetchDebug("setting Host header: %.*s", hostNameLen, hostName);
  } else {
    PrefetchError("failed to set Host header: %.*s", hostNameLen, hostName);
  }

  /* Save the URL to be fetched with this fetch for debugging purposes, expensive TSUrlStringGet()
   * but really helpful when debugging multi-remap / host-replacement use cases */
  int urlLen = 0;
  char *url  = TSUrlStringGet(_mbuf, _urlLoc, &urlLen);
  if (nullptr != url) {
    _url.assign(url, urlLen);
    TSfree(static_cast<void *>(url));
  }

  /* TODO: TBD is this the right place? */
  if (TS_SUCCESS != TSHttpHdrUrlSet(_mbuf, _headerLoc, _urlLoc)) {
    return false;
  }

  /* Initialization is success */
  return true;
}

/**
 * @brief Create, setup and schedule the background fetch continuation.
 */
void
BgFetch::schedule()
{
  TSAssert(nullptr == _cont);

  /* Setup the continuation */
  _cont = TSContCreate(handler, TSMutexCreate());
  TSContDataSet(_cont, static_cast<void *>(this));

  /* Initialize the VIO (for the fetch) */
  req_io_buf         = TSIOBufferCreate();
  req_io_buf_reader  = TSIOBufferReaderAlloc(req_io_buf);
  resp_io_buf        = TSIOBufferCreate();
  resp_io_buf_reader = TSIOBufferReaderAlloc(resp_io_buf);

  /* Schedule */
  PrefetchDebug("schedule fetch: %s", _url.c_str());
  _startTime = TShrtime();
  TSContSchedule(_cont, 0, TS_THREAD_POOL_NET);
}

/* Log format is: name-space bytes status url */
void
BgFetch::logAndMetricUpdate(TSEvent event) const
{
  const char *status;

  switch (event) {
  case TS_EVENT_VCONN_EOS:
    status = "EOS";
    _state->incrementMetric(FETCH_COMPLETED);
    break;
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
    status = "TIMEOUT";
    _state->incrementMetric(FETCH_TIMEOOUTS);
    break;
  case TS_EVENT_ERROR:
    _state->incrementMetric(FETCH_ERRORS);
    status = "ERROR";
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
    _state->incrementMetric(FETCH_COMPLETED);
    status = "READ_COMP";
    break;
  default:
    status = "UNKNOWN";
    break;
  }

  if (TSIsDebugTagSet(PLUGIN_NAME "_log")) {
    TSHRTime now   = TShrtime();
    double elapsed = (double)(now - _startTime) / 1000000.0;

    PrefetchDebug("ns=%s bytes=%" PRId64 " time=%1.3lf status=%s url=%s key=%s", _config.getNameSpace().c_str(), _bytes, elapsed,
                  status, _url.c_str(), _cachekey.c_str());
    if (_state->getLog()) {
      TSTextLogObjectWrite(_state->getLog(), "ns=%s bytes=%" PRId64 " time=%1.3lf status=%s url=%s key=%s",
                           _config.getNameSpace().c_str(), _bytes, elapsed, status, _url.c_str(), _cachekey.c_str());
    }
  }
}

/**
 * @brief Continuation to perform a background fill of a URL.
 *
 * This is pretty expensive (memory allocations etc.)
 */
int
BgFetch::handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  BgFetch *fetch = static_cast<BgFetch *>(TSContDataGet(contp));
  int64_t avail;

  PrefetchDebug("event: %s (%d)", TSHttpEventNameLookup(event), event);

  switch (event) {
  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_TIMEOUT:
    // Debug info for this particular bg fetch (put all debug in here please)
    if (TSIsDebugTagSet(PLUGIN_NAME)) {
      char buf[INET6_ADDRSTRLEN];
      const sockaddr *sockaddress = (const sockaddr *)&fetch->client_ip;

      switch (sockaddress->sa_family) {
      case AF_INET:
        inet_ntop(AF_INET, &(((struct sockaddr_in *)sockaddress)->sin_addr), buf, INET_ADDRSTRLEN);
        PrefetchDebug("client IPv4 = %s", buf);
        break;
      case AF_INET6:
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sockaddress)->sin6_addr), buf, INET6_ADDRSTRLEN);
        PrefetchDebug("client IPv6 = %s", buf);
        break;
      default:
        TSError("[%s] Unknown address family %d", PLUGIN_NAME, sockaddress->sa_family);
        break;
      }
      PrefetchDebug("Starting background fetch.");
      dumpHeaders(fetch->_mbuf, fetch->_headerLoc);
    }

    // Setup the NetVC for background fetch
    TSAssert(nullptr == fetch->vc);
    if ((fetch->vc = TSHttpConnect((sockaddr *)&fetch->client_ip)) != nullptr) {
      TSHttpHdrPrint(fetch->_mbuf, fetch->_headerLoc, fetch->req_io_buf);
      // We never send a body with the request. ToDo: Do we ever need to support that ?
      TSIOBufferWrite(fetch->req_io_buf, "\r\n", 2);

      fetch->r_vio = TSVConnRead(fetch->vc, contp, fetch->resp_io_buf, INT64_MAX);
      fetch->w_vio = TSVConnWrite(fetch->vc, contp, fetch->req_io_buf_reader, TSIOBufferReaderAvail(fetch->req_io_buf_reader));
    } else {
      delete fetch;
      PrefetchError("Failed to connect to internal process, major malfunction");
    }
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    // TSVConnShutdown(data->vc, 0, 1);
    // TSVIOReenable(data->w_vio);
    PrefetchDebug("write complete");
    break;

  case TS_EVENT_VCONN_READ_READY:
    avail = TSIOBufferReaderAvail(fetch->resp_io_buf_reader);
    fetch->addBytes(avail);
    TSIOBufferReaderConsume(fetch->resp_io_buf_reader, avail);
    TSVIONDoneSet(fetch->r_vio, TSVIONDoneGet(fetch->r_vio) + avail);
    TSVIOReenable(fetch->r_vio);
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
  case TS_EVENT_ERROR:
    if (event == TS_EVENT_VCONN_INACTIVITY_TIMEOUT) {
      PrefetchDebug("encountered Inactivity Timeout");
      TSVConnAbort(fetch->vc, TS_VC_CLOSE_ABORT);
    } else {
      TSVConnClose(fetch->vc);
    }

    PrefetchDebug("closing background transaction");
    avail = TSIOBufferReaderAvail(fetch->resp_io_buf_reader);
    fetch->addBytes(avail);
    TSIOBufferReaderConsume(fetch->resp_io_buf_reader, avail);
    TSVIONDoneSet(fetch->r_vio, TSVIONDoneGet(fetch->r_vio) + avail);
    fetch->logAndMetricUpdate(event);

    /* Close, release and cleanup */
    fetch->vc = nullptr;
    delete fetch;
    break;

  default:
    PrefetchDebug("unhandled event");
    break;
  }

  return 0;
}
