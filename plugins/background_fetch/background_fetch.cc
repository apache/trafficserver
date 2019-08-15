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
#include <unordered_map>
#include <cinttypes>
#include <string_view>
#include <array>

#include "ts/ts.h"
#include "ts/remap.h"
#include "headers.h"
#include "rules.h"
#include "configs.h"

// Global config, if we don't have a remap specific config.
static BgFetchConfig *gConfig = nullptr;

// This is the list of all headers that must be removed when we make the actual background
// fetch request.
static const std::array<const std::string_view, 6> FILTER_HEADERS{
  {{TS_MIME_FIELD_RANGE, static_cast<size_t>(TS_MIME_LEN_RANGE)},
   {TS_MIME_FIELD_IF_MATCH, static_cast<size_t>(TS_MIME_LEN_IF_MATCH)},
   {TS_MIME_FIELD_IF_MODIFIED_SINCE, static_cast<size_t>(TS_MIME_LEN_IF_MODIFIED_SINCE)},
   {TS_MIME_FIELD_IF_NONE_MATCH, static_cast<size_t>(TS_MIME_LEN_IF_NONE_MATCH)},
   {TS_MIME_FIELD_IF_RANGE, static_cast<size_t>(TS_MIME_LEN_IF_RANGE)},
   {TS_MIME_FIELD_IF_UNMODIFIED_SINCE, static_cast<size_t>(TS_MIME_LEN_IF_UNMODIFIED_SINCE)}}};

///////////////////////////////////////////////////////////////////////////
// Hold the global background fetch state. This is currently shared across all
// configurations, as a singleton. ToDo: Would it ever make sense to do this
// per remap rule? Maybe for per-remap logging ??
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

            // Next, remove the Range headers and IMS (conditional) headers from the request
            for (auto const &header : FILTER_HEADERS) {
              if (remove_header(mbuf, hdr_loc, header.data(), header.size()) > 0) {
                TSDebug(PLUGIN_NAME, "Removed the %s header from request", header.data());
              }
            }

            // Everything went as planned, so we can return true
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

///////////////////////////////////////////////////////////////////////////
// This is a TXN hook, used to verify that the response (before sending to
// originating client) is indeed cacheable. This has to be deferred, because
// we might have plugins that changes the cacheability of an Origin response.
//
static int
cont_check_cacheable(TSCont contp, TSEvent /* event ATS_UNUSED */, void *edata)
{
  // ToDo: If we want to support per-remap configurations, we have to pass along the data here
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  TSMBuffer response, request;
  TSMLoc resp_hdr, req_hdr;

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &response, &resp_hdr)) {
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &request, &req_hdr)) {
      // Temporarily change the response status to 200 OK, so we can reevaluate cacheability.
      TSHttpHdrStatusSet(response, resp_hdr, TS_HTTP_STATUS_OK);
      bool cacheable = TSHttpTxnIsCacheable(txnp, nullptr, response);
      TSHttpHdrStatusSet(response, resp_hdr, TS_HTTP_STATUS_PARTIAL_CONTENT);

      TSDebug(PLUGIN_NAME, "Testing: request / response is cacheable?");
      if (cacheable) {
        BgFetchData *data = new BgFetchData();

        // Initialize the data structure (can fail) and acquire a privileged lock on the URL
        if (data->initialize(request, req_hdr, txnp) && data->acquireUrl()) {
          data->schedule();
        } else {
          delete data; // Not sure why this would happen, but ok.
        }
      }
      // Release the request MLoc
      TSHandleMLocRelease(request, TS_NULL_MLOC, req_hdr);
    }
    // Release the response MLoc
    TSHandleMLocRelease(response, TS_NULL_MLOC, resp_hdr);
  }

  // Reenable and continue with the state machine.
  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Main "plugin", which is a global READ_RESPONSE_HDR hook. Before
// initiating a background fetch, this checks:
//
//     1. Check if a background fetch is allowed for this request
// and
//     2. Is the response from origin a 206 (Partial)?
//
// We defer the check on cacheability to the SEND_RESPONSE_HDR hook, since
// there could be other plugins that modifies the response after us.
//
static int
cont_handle_response(TSCont contp, TSEvent event, void *edata)
{
  // ToDo: If we want to support per-remap configurations, we have to pass along the data here
  TSHttpTxn txnp        = static_cast<TSHttpTxn>(edata);
  BgFetchConfig *config = static_cast<BgFetchConfig *>(TSContDataGet(contp));

  if (nullptr == config) {
    // something seriously wrong..
    TSError("[%s] Can't get configurations", PLUGIN_NAME);
  } else {
    switch (event) {
    case TS_EVENT_HTTP_READ_RESPONSE_HDR:
      if (config->bgFetchAllowed(txnp)) {
        TSMBuffer response;
        TSMLoc resp_hdr;

        if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &response, &resp_hdr)) {
          // ToDo: Check the MIME type first, to see if it's a type we care about.
          // ToDo: Such MIME types should probably be per remap rule.

          // Only deal with 206 and possibly 304 responses from Origin
          TSHttpStatus status = TSHttpHdrStatusGet(response, resp_hdr);
          TSDebug(PLUGIN_NAME, "Testing: response status code: %d?", status);
          if (TS_HTTP_STATUS_PARTIAL_CONTENT == status || (config->allow304() && TS_HTTP_STATUS_NOT_MODIFIED == status)) {
            // Everything looks good so far, add a TXN hook for SEND_RESPONSE_HDR
            TSCont contp = TSContCreate(cont_check_cacheable, nullptr);

            TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
          }
          // Release the response MLoc
          TSHandleMLocRelease(response, TS_NULL_MLOC, resp_hdr);
        }
      }
      break;
    default:
      TSError("[%s] Unknown event for this plugin", PLUGIN_NAME);
      TSDebug(PLUGIN_NAME, "unknown event for this plugin");
      break;
    }
  }

  // Reenable and continue with the state machine.
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

///////////////////////////////////////////////////////////////////////////
// Setup global hooks
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  TSCont cont = TSContCreate(cont_handle_response, nullptr);

  gConfig = new BgFetchConfig(cont);

  if (gConfig->parseOptions(argc, argv)) {
    // Create the global log file. Note that calling this multiple times currently has no
    // effect, only one log file is ever created. The BgFetchState is a singleton.
    if (!gConfig->logFile().empty()) {
      BgFetchState::getInstance().createLog(gConfig->logFile());
    }
    TSDebug(PLUGIN_NAME, "Initialized");
    TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
  } else {
    // ToDo: Hmmm, no way to fail a global plugin here?
    TSDebug(PLUGIN_NAME, "Failed to initialize as global plugin");
  }
}

///////////////////////////////////////////////////////////////////////////
// Setup Remap mode
///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "background fetch remap init");
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "background fetch remap is successfully initialized");
  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// We don't have any specific "instances" here, at least not yet.
//
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  TSCont cont           = TSContCreate(cont_handle_response, nullptr);
  BgFetchConfig *config = new BgFetchConfig(cont);
  bool success          = true;

  // The first two arguments are the "from" and "to" URL string. We need to
  // skip them, but we also require that there be an option to masquerade as
  // argv[0], so we increment the argument indexes by 1 rather than by 2.
  argc--;
  argv++;

  // This is for backwards compatibility, ugly! ToDo: Remove for ATS v9.0.0 IMO.
  if (argc > 1 && *argv[1] != '-') {
    TSDebug(PLUGIN_NAME, "config file %s", argv[1]);
    if (!config->readConfig(argv[1])) {
      success = false;
    }
  } else {
    if (config->parseOptions(argc, const_cast<const char **>(argv))) {
      // Create the global log file. Remember, the BgFetchState is a singleton.
      if (config->logFile().size()) {
        BgFetchState::getInstance().createLog(config->logFile());
      }
    } else {
      success = false;
    }
  }

  if (success) {
    *ih = config;

    return TS_SUCCESS;
  }

  // Something went wrong with the configuration setup.
  delete config;
  return TS_ERROR;
}

void
TSRemapDeleteInstance(void *ih)
{
  BgFetchConfig *config = static_cast<BgFetchConfig *>(ih);

  TSContDestroy(config->getCont());
  delete config;
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

  TSMBuffer bufp;
  TSMLoc req_hdrs;

  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &req_hdrs)) {
    TSMLoc field_loc = TSMimeHdrFieldFind(bufp, req_hdrs, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);

    if (!field_loc) { // Less common case, but also allow If-Range header to trigger, but only if Range not present
      field_loc = TSMimeHdrFieldFind(bufp, req_hdrs, TS_MIME_FIELD_IF_RANGE, TS_MIME_LEN_IF_RANGE);
    }

    if (field_loc) {
      BgFetchConfig *config = static_cast<BgFetchConfig *>(ih);

      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, config->getCont());
      TSDebug(PLUGIN_NAME, "TSRemapDoRemap() added hook, request was Range / If-Range");
      TSHandleMLocRelease(bufp, req_hdrs, field_loc);
    }
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, req_hdrs);
  }

  return TSREMAP_NO_REMAP;
}
