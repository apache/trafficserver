/** @file

    Plugin to perform background fetches of certain content that would
    otherwise not be cached. For example, Range: requests / responses.

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
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include <string>
#include <iostream>
#include <unordered_map>
#include <cinttypes>
#include <string_view>
#include <array>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ts/ts.h"
#include "ts/remap.h"
#include "headers.h"

///////////////////////////////////////////////////////////////////////////
typedef std::unordered_map<std::string, bool> OutstandingRequests;

class BgFetchState
{
public:
  BgFetchState()                     = default;
  BgFetchState(BgFetchState const &) = delete;
  void operator=(BgFetchState const &) = delete;

  static BgFetchState &
  getInstance()
  {
    static BgFetchState _instance;
    return _instance;
  }

  ~BgFetchState() { TSMutexDestroy(_lock); }

  void
  createLog(const std::string &log_name)
  {
    if (!_log) {
      TSDebug(PLUGIN_NAME, "Creating log name %s", log_name.c_str());
      TSAssert(TS_SUCCESS == TSTextLogObjectCreate(log_name.c_str(), TS_LOG_MODE_ADD_TIMESTAMP, &_log));
    } else {
      TSError("[%s] A log file was already create, ignoring creation of %s", PLUGIN_NAME, log_name.c_str());
    }
  }

  TSTextLogObject
  getLog()
  {
    return _log;
  }

  bool
  acquire(const std::string &url)
  {
    bool ret;

    TSMutexLock(_lock);
    if (_urls.end() == _urls.find(url)) {
      _urls[url] = true;
      ret        = true;
    } else {
      ret = false;
    }
    TSMutexUnlock(_lock);

    TSDebug(PLUGIN_NAME, "BgFetchState.acquire(): ret = %d, url = %s", ret, url.c_str());

    return ret;
  }

  bool
  release(const std::string &url)
  {
    bool ret;

    TSMutexLock(_lock);
    if (_urls.end() == _urls.find(url)) {
      ret = false;
    } else {
      _urls.erase(url);
      ret = true;
    }
    TSMutexUnlock(_lock);

    return ret;
  }

private:
  OutstandingRequests _urls;
  TSTextLogObject _log = nullptr;
  TSMutex _lock        = TSMutexCreate();
};

//////////////////////////////////////////////////////////////////////////////
// Hold and manage some state for the TXN background fetch continuation.
// This is necessary, because the TXN is likely to not be available
// during the time we fetch from origin.
struct BgFetchData {
  BgFetchData() { memset(&client_ip, 0, sizeof(client_ip)); }

  ~BgFetchData()
  {
    TSHandleMLocRelease(mbuf, TS_NULL_MLOC, hdr_loc);
    TSHandleMLocRelease(mbuf, TS_NULL_MLOC, url_loc);

    TSMBufferDestroy(mbuf);

    if (vc) {
      TSError("[%s] Destroyed BgFetchDATA while VC was alive", PLUGIN_NAME);
      TSVConnClose(vc);
      vc = nullptr;
    }

    // If we got schedule, also clean that up
    if (_cont) {
      releaseUrl();

      TSContDestroy(_cont);
      _cont = nullptr;
      TSIOBufferReaderFree(req_io_buf_reader);
      TSIOBufferDestroy(req_io_buf);
      TSIOBufferReaderFree(resp_io_buf_reader);
      TSIOBufferDestroy(resp_io_buf);
    }
  }

  bool
  acquireUrl() const
  {
    return BgFetchState::getInstance().acquire(_url);
  }
  bool
  releaseUrl() const
  {
    return BgFetchState::getInstance().release(_url);
  }

  const char *
  getUrl() const
  {
    return _url.c_str();
  }

  void
  addBytes(int64_t b)
  {
    _bytes += b;
  }

  bool initialize(TSMBuffer request, TSMLoc req_hdr, TSHttpTxn txnp);
  void schedule();
  void log(TSEvent event) const;

  TSMBuffer mbuf = TSMBufferCreate();
  TSMLoc hdr_loc = TS_NULL_MLOC;
  TSMLoc url_loc = TS_NULL_MLOC;

  struct sockaddr_storage client_ip;

  // This is for the actual background fetch / NetVC
  TSVConn vc                          = nullptr;
  TSIOBuffer req_io_buf               = nullptr;
  TSIOBuffer resp_io_buf              = nullptr;
  TSIOBufferReader req_io_buf_reader  = nullptr;
  TSIOBufferReader resp_io_buf_reader = nullptr;
  TSVIO r_vio                         = nullptr;
  TSVIO w_vio                         = nullptr;

private:
  std::string _url;
  int64_t _bytes = 0;
  TSCont _cont   = nullptr;
};

// This sets up the data and continuation properly, this is done outside
// of the CTor, since this can actually fail. If we fail, the data is
// useless, and should be delete'd.
//
// This needs the txnp temporarily, so it can copy the pristine request
// URL. The txnp is not used once initialize() returns.
//
// Upon successful completion, the struct should be ready to start a
// background fetch.
bool
BgFetchData::initialize(TSMBuffer request, TSMLoc req_hdr, TSHttpTxn txnp)
{
  struct sockaddr const *ip = TSHttpTxnClientAddrGet(txnp);
  bool ret                  = false;

  TSAssert(TS_NULL_MLOC == hdr_loc);
  TSAssert(TS_NULL_MLOC == url_loc);

  if (ip) {
    if (ip->sa_family == AF_INET) {
      memcpy(&client_ip, ip, sizeof(sockaddr_in));
    } else if (ip->sa_family == AF_INET6) {
      memcpy(&client_ip, ip, sizeof(sockaddr_in6));
    } else {
      TSError("[%s] Unknown address family %d", PLUGIN_NAME, ip->sa_family);
    }
  } else {
    TSError("[%s] Failed to get client host info", PLUGIN_NAME);
    return false;
  }

  hdr_loc = TSHttpHdrCreate(mbuf);
  if (TS_SUCCESS == TSHttpHdrCopy(mbuf, hdr_loc, request, req_hdr)) {
    TSMLoc p_url;

    // Now copy the pristine request URL into our MBuf
    if (TS_SUCCESS == TSHttpTxnPristineUrlGet(txnp, &request, &p_url)) {
      if (TS_SUCCESS == TSUrlClone(mbuf, request, p_url, &url_loc)) {
        TSMLoc c_url = TS_NULL_MLOC;
        int len;
        char *url = nullptr;

        // Get the cache key URL (for now), since this has better lookup behavior when using
        // e.g. the cachekey plugin.
        if (TS_SUCCESS == TSUrlCreate(request, &c_url)) {
          if (TS_SUCCESS == TSHttpTxnCacheLookupUrlGet(txnp, request, c_url)) {
            url = TSUrlStringGet(request, c_url, &len);
            TSHandleMLocRelease(request, TS_NULL_MLOC, c_url);
            TSDebug(PLUGIN_NAME, "Cache URL is %.*s", len, url);
          }
        }

        if (url) {
          _url.assign(url, len); // Save away the cache URL for later use when acquiring lock
          TSfree(static_cast<void *>(url));

          if (TS_SUCCESS == TSHttpHdrUrlSet(mbuf, hdr_loc, url_loc)) {
            // Make sure we have the correct Host: header for this request.
            const char *hostp = TSUrlHostGet(mbuf, url_loc, &len);

            if (set_header(mbuf, hdr_loc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST, hostp, len)) {
              TSDebug(PLUGIN_NAME, "Set header Host: %.*s", len, hostp);
            }
            ret = true;
          }
        }
      }
      TSHandleMLocRelease(request, TS_NULL_MLOC, p_url);
    }
  }

  return ret;
}

static int cont_bg_fetch(TSCont contp, TSEvent event, void *edata);

// Create, setup and schedule the background fetch continuation.
void
BgFetchData::schedule()
{
  TSAssert(nullptr == _cont);

  // Setup the continuation
  _cont = TSContCreate(cont_bg_fetch, TSMutexCreate());
  TSContDataSet(_cont, static_cast<void *>(this));

  // Initialize the VIO stuff (for the fetch)
  req_io_buf         = TSIOBufferCreate();
  req_io_buf_reader  = TSIOBufferReaderAlloc(req_io_buf);
  resp_io_buf        = TSIOBufferCreate();
  resp_io_buf_reader = TSIOBufferReaderAlloc(resp_io_buf);

  // Schedule
  TSContScheduleOnPool(_cont, 0, TS_THREAD_POOL_NET);
}

// Log format is:
//    remap-tag bytes status url
void
BgFetchData::log(TSEvent event) const
{
  TSTextLogObject log = BgFetchState::getInstance().getLog();

  if (log || TSIsDebugTagSet(PLUGIN_NAME)) {
    const char *status;

    switch (event) {
    case TS_EVENT_VCONN_EOS:
      status = "EOS";
      break;
    case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
      status = "TIMEOUT";
      break;
    case TS_EVENT_ERROR:
      status = "ERROR";
      break;
    case TS_EVENT_VCONN_READ_COMPLETE:
      status = "READ_COMP";
      break;
    default:
      status = "UNKNOWN";
      break;
    }

    // ToDo: Also deal with per-remap tagging
    TSDebug(PLUGIN_NAME, "%s %" PRId64 " %s %s", "-", _bytes, status, _url.c_str());
    if (log) {
      TSTextLogObjectWrite(log, "%s %" PRId64 " %s %s", "-", _bytes, status, _url.c_str());
    }
  }
}

//////////////////////////////////////////////////////////////////////////////
// Continuation to perform a background fill of a URL. This is pretty
// expensive (memory allocations etc.), we could eliminate maybe the
// std::string, but I think it's fine for now.
static int
cont_bg_fetch(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  BgFetchData *data = static_cast<BgFetchData *>(TSContDataGet(contp));
  int64_t avail;

  switch (event) {
  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_TIMEOUT:
    // Debug info for this particular bg fetch (put all debug in here please)
    if (TSIsDebugTagSet(PLUGIN_NAME)) {
      char buf[INET6_ADDRSTRLEN];
      const sockaddr *sockaddress = reinterpret_cast<const sockaddr *>(&data->client_ip);

      switch (sockaddress->sa_family) {
      case AF_INET:
        inet_ntop(AF_INET, &(((struct sockaddr_in *)sockaddress)->sin_addr), buf, INET_ADDRSTRLEN);
        TSDebug(PLUGIN_NAME, "Client IPv4 = %s", buf);
        break;
      case AF_INET6:
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sockaddress)->sin6_addr), buf, INET6_ADDRSTRLEN);
        TSDebug(PLUGIN_NAME, "Client IPv6 = %s", buf);
        break;
      default:
        TSError("[%s] Unknown address family %d", PLUGIN_NAME, sockaddress->sa_family);
        break;
      }
      TSDebug(PLUGIN_NAME, "Starting background fetch, replaying:");
      dump_headers(data->mbuf, data->hdr_loc);
    }

    // Setup the NetVC for background fetch
    TSAssert(nullptr == data->vc);
    if ((data->vc = TSHttpConnectWithPluginId(reinterpret_cast<sockaddr *>(&data->client_ip), PLUGIN_NAME, 0)) != nullptr) {
      TSHttpHdrPrint(data->mbuf, data->hdr_loc, data->req_io_buf);
      // We never send a body with the request. ToDo: Do we ever need to support that ?
      TSIOBufferWrite(data->req_io_buf, "\r\n", 2);

      data->r_vio = TSVConnRead(data->vc, contp, data->resp_io_buf, INT64_MAX);
      data->w_vio = TSVConnWrite(data->vc, contp, data->req_io_buf_reader, TSIOBufferReaderAvail(data->req_io_buf_reader));
    } else {
      delete data;
      TSError("[%s] Failed to connect to internal process, major malfunction", PLUGIN_NAME);
    }
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    // TSVConnShutdown(data->vc, 0, 1);
    // TSVIOReenable(data->w_vio);
    TSDebug(PLUGIN_NAME, "Write Complete");
    break;

  case TS_EVENT_VCONN_READ_READY:
    avail = TSIOBufferReaderAvail(data->resp_io_buf_reader);
    data->addBytes(avail);
    TSIOBufferReaderConsume(data->resp_io_buf_reader, avail);
    TSVIONDoneSet(data->r_vio, TSVIONDoneGet(data->r_vio) + avail);
    TSVIOReenable(data->r_vio);
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
  case TS_EVENT_ERROR:
    if (event == TS_EVENT_VCONN_INACTIVITY_TIMEOUT) {
      TSDebug(PLUGIN_NAME, "Encountered Inactivity Timeout");
      TSVConnAbort(data->vc, TS_VC_CLOSE_ABORT);
    } else {
      TSVConnClose(data->vc);
    }

    TSDebug(PLUGIN_NAME, "Closing down background transaction, event= %s(%d)", TSHttpEventNameLookup(event), event);
    avail = TSIOBufferReaderAvail(data->resp_io_buf_reader);
    data->addBytes(avail);
    TSIOBufferReaderConsume(data->resp_io_buf_reader, avail);
    TSVIONDoneSet(data->r_vio, TSVIONDoneGet(data->r_vio) + avail);
    data->log(event);

    // Close, release and cleanup
    data->vc = nullptr;
    delete data;
    break;

  default:
    TSDebug(PLUGIN_NAME, "Unhandled event: %s (%d)", TSHttpEventNameLookup(event), event);
    break;
  }

  return 0;
}
static const char *
getCacheLookupResultName(TSCacheLookupResult result)
{
  switch (result) {
  case TS_CACHE_LOOKUP_MISS:
    return "TS_CACHE_LOOKUP_MISS";
    break;
  case TS_CACHE_LOOKUP_HIT_STALE:
    return "TS_CACHE_LOOKUP_HIT_STALE";
    break;
  case TS_CACHE_LOOKUP_HIT_FRESH:
    return "TS_CACHE_LOOKUP_HIT_FRESH";
    break;
  case TS_CACHE_LOOKUP_SKIPPED:
    return "TS_CACHE_LOOKUP_SKIPPED";
    break;
  default:
    return "UNKNOWN_CACHE_LOOKUP_EVENT";
    break;
  }
  return "UNKNOWN_CACHE_LOOKUP_EVENT";
}

///////////////////////////////////////////////////////////////////////////
// create background fetch request if possible
//
static bool
cont_check_cacheable(TSHttpTxn txnp)
{
  if (TSHttpTxnIsInternal(txnp))
    return false;
  int lookupStatus;
  TSHttpTxnCacheLookupStatusGet(txnp, &lookupStatus);
  TSDebug(PLUGIN_NAME, "lookup status: %s", getCacheLookupResultName((TSCacheLookupResult)lookupStatus));
  bool ret = false;
  if (TS_CACHE_LOOKUP_MISS == lookupStatus || TS_CACHE_LOOKUP_HIT_STALE == lookupStatus) {
    bool const nostore = TSHttpTxnServerRespNoStoreGet(txnp);

    TSDebug(PLUGIN_NAME, "is nostore set %d", nostore);
    if (!nostore) {
      TSMBuffer request;
      TSMLoc req_hdr;
      if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &request, &req_hdr)) {
        BgFetchData *data = new BgFetchData();
        // Initialize the data structure (can fail) and acquire a privileged lock on the URL
        if (data->initialize(request, req_hdr, txnp) && data->acquireUrl()) {
          TSDebug(PLUGIN_NAME, "scheduling background fetch");
          data->schedule();
          ret = true;
        } else {
          delete data;
        }
      }
      TSHandleMLocRelease(request, TS_NULL_MLOC, req_hdr);
    }
  }
  return ret;
}

//////////////////////////////////////////////////////////////////////////////
// Main "plugin", which is a global TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE hook. Before
// initiating a background fetch, this checks
// if a background fetch is allowed for this request
//
static int
cont_handle_cache(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  if (TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE == event) {
    bool const requested = cont_check_cacheable(txnp);
    if (requested) // Made a background fetch request, do not cache the response
    {
      TSDebug(PLUGIN_NAME, "setting no store");
      TSHttpTxnServerRespNoStoreSet(txnp, 1);
    }

  } else {
    TSError("[%s] Unknown event for this plugin %d", PLUGIN_NAME, event);
    TSDebug(PLUGIN_NAME, "unknown event for this plugin %d", event);
  }

  // Reenable and continue with the state machine.
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

///////////////////////////////////////////////////////////////////////////
// Setup Remap mode
///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "cache fill remap init");
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "cache fill remap is successfully initialized");
  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// We don't have any specific "instances" here, at least not yet.
//
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  TSCont cont = TSContCreate(cont_handle_cache, nullptr);
  *ih         = cont;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  TSCont cont = static_cast<TSCont>(ih);
  TSContDestroy(cont);
}

///////////////////////////////////////////////////////////////////////////////
//// This is the main "entry" point for the plugin, called for every request.
////
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri */)
{
  if (nullptr == ih) {
    return TSREMAP_NO_REMAP;
  }
  TSCont const cont = static_cast<TSCont>(ih);
  TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
  TSDebug(PLUGIN_NAME, "TSRemapDoRemap() added hook");

  return TSREMAP_NO_REMAP;
}
