/** @file
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

#include "slice.h"

#include "Config.h"
#include "Data.h"
#include "HttpHeader.h"
#include "intercept.h"

#include "ts/apidefs.h"
#include "ts/remap.h"
#include "ts/ts.h"

#include <charconv>
#include <netinet/in.h>
#include <array>
#include <string_view>

namespace
{
using namespace std::string_view_literals;
constexpr std::string_view SKIP_CRR_HDR_NAME  = "X-Skip-Crr"sv;
constexpr std::string_view SKIP_CRR_HDR_VALUE = "-"sv;

struct PluginInfo {
  Config config;
  TSCont read_resp_hdr_contp;
};

Config globalConfig;
TSCont global_read_resp_hdr_contp;

static bool
should_skip_this_obj(TSHttpTxn txnp, Config *const config)
{
  int         len    = 0;
  char *const urlstr = TSHttpTxnEffectiveUrlStringGet(txnp, &len);

  if (!config->isKnownLargeObj({urlstr, static_cast<size_t>(len)})) {
    DEBUG_LOG("Not a known large object, not slicing: %.*s", len, urlstr);
    return true;
  }

  return false;
}

bool
read_request(TSHttpTxn txnp, Config *const config, TSCont read_resp_hdr_contp)
{
  DEBUG_LOG("slice read_request");
  TxnHdrMgr hdrmgr;
  hdrmgr.populateFrom(txnp, TSHttpTxnClientReqGet);
  HttpHeader const header(hdrmgr.m_buffer, hdrmgr.m_lochdr);

  if (TS_HTTP_METHOD_GET == header.method() || TS_HTTP_METHOD_HEAD == header.method() || TS_HTTP_METHOD_PURGE == header.method()) {
    if (!header.hasKey(config->m_skip_header.data(), config->m_skip_header.size())) {
      // check if any previous plugin has monkeyed with the transaction status
      TSHttpStatus const txnstat = TSHttpTxnStatusGet(txnp);
      if (TS_HTTP_STATUS_NONE != txnstat) {
        DEBUG_LOG("txn status change detected (%d), skipping plugin\n", static_cast<int>(txnstat));
        return false;
      }

      if (config->hasRegex()) {
        int         urllen = 0;
        char *const urlstr = TSHttpTxnEffectiveUrlStringGet(txnp, &urllen);
        if (nullptr != urlstr) {
          bool const shouldslice = config->matchesRegex(urlstr, urllen);
          if (!shouldslice) {
            DEBUG_LOG("request failed regex, not slicing: '%.*s'", urllen, urlstr);
            TSfree(urlstr);
            return false;
          }

          DEBUG_LOG("request passed regex, slicing: '%.*s'", urllen, urlstr);
          TSfree(urlstr);
        }
      }

      DEBUG_LOG("slice accepting and slicing");
      // connection back into ATS
      sockaddr const *const ip = TSHttpTxnClientAddrGet(txnp);
      if (nullptr == ip) {
        return false;
      }

      TSAssert(nullptr != config);
      std::unique_ptr<Data> data = std::make_unique<Data>(config);

      data->m_method_type = header.method();
      data->m_txnp        = txnp;

      // set up feedback connect
      if (AF_INET == ip->sa_family) {
        memcpy(&data->m_client_ip, ip, sizeof(sockaddr_in));
      } else if (AF_INET6 == ip->sa_family) {
        memcpy(&data->m_client_ip, ip, sizeof(sockaddr_in6));
      } else {
        return false;
      }

      // need to reset the HOST field for global plugin
      data->m_hostlen = sizeof(data->m_hostname) - 1;
      if (!header.valueForKey(TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST, data->m_hostname, &data->m_hostlen)) {
        DEBUG_LOG("Unable to get hostname from header");
        return false;
      }

      // check if object is large enough to slice - skip small and unknown size objects
      if (should_skip_this_obj(txnp, config)) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, read_resp_hdr_contp);
        TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, read_resp_hdr_contp);

        // If the client sends a range request, and we don't slice, the range request goes to cache_range_requests, and can be
        // cached as a range request. We don't want that, and instead want to skip CRR entirely.
        if (header.hasKey(TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE)) {
          HttpHeader mutable_header(hdrmgr.m_buffer, hdrmgr.m_lochdr);
          mutable_header.setKeyVal(SKIP_CRR_HDR_NAME.data(), SKIP_CRR_HDR_NAME.length(), SKIP_CRR_HDR_VALUE.data(),
                                   SKIP_CRR_HDR_VALUE.length());
        }
        return false;
      }

      // is the plugin configured to use a remap host?
      std::string const &newhost = config->m_remaphost;
      if (newhost.empty()) {
        TSMBuffer    urlbuf = nullptr;
        TSMLoc       urlloc = nullptr;
        TSReturnCode rcode  = TSHttpTxnPristineUrlGet(txnp, &urlbuf, &urlloc);

        if (TS_SUCCESS == rcode) {
          TSMBuffer const newbuf = TSMBufferCreate();
          TSMLoc          newloc = nullptr;
          rcode                  = TSUrlClone(newbuf, urlbuf, urlloc, &newloc);
          TSHandleMLocRelease(urlbuf, TS_NULL_MLOC, urlloc);

          if (TS_SUCCESS != rcode) {
            ERROR_LOG("Error cloning pristine url");
            TSMBufferDestroy(newbuf);
            return false;
          }

          data->m_urlbuf = newbuf;
          data->m_urlloc = newloc;
        }
      } else { // grab the effective url, swap out the host and zero the port
        int         len    = 0;
        char *const effstr = TSHttpTxnEffectiveUrlStringGet(txnp, &len);

        if (nullptr != effstr) {
          TSMBuffer const newbuf = TSMBufferCreate();
          TSMLoc          newloc = nullptr;
          bool            okay   = false;

          if (TS_SUCCESS == TSUrlCreate(newbuf, &newloc)) {
            char const *start = effstr;
            if (TS_PARSE_DONE == TSUrlParse(newbuf, newloc, &start, start + len)) {
              if (TS_SUCCESS == TSUrlHostSet(newbuf, newloc, newhost.c_str(), newhost.size()) &&
                  TS_SUCCESS == TSUrlPortSet(newbuf, newloc, 0)) {
                okay = true;
              }
            }
          }

          TSfree(effstr);

          if (!okay) {
            ERROR_LOG("Error cloning effective url");
            if (nullptr != newloc) {
              TSHandleMLocRelease(newbuf, nullptr, newloc);
            }
            TSMBufferDestroy(newbuf);
            return false;
          }

          data->m_urlbuf = newbuf;
          data->m_urlloc = newloc;
        }
      }

      data->m_buffer_index      = TSPluginVCIOBufferIndexGet(data->m_txnp);     // default of m_buffer_index = 32KB
      data->m_buffer_water_mark = TSPluginVCIOBufferWaterMarkGet(data->m_txnp); // default of m_buffer_water_mark = 0

      if (dbg_ctl.on()) {
        int         len    = 0;
        char *const urlstr = TSUrlStringGet(data->m_urlbuf, data->m_urlloc, &len);
        DEBUG_LOG("slice url: %.*s", len, urlstr);
        TSfree(urlstr);
      }

      // we'll intercept this GET and do it ourselves
      TSMutex const mutex = TSContMutexGet(reinterpret_cast<TSCont>(txnp));
      TSCont const  icontp(TSContCreate(intercept_hook, mutex));
      TSContDataSet(icontp, data.release());

      // Skip remap and remap rule requirement
      TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_SKIP_REMAPPING, true);

      // Grab the transaction
      TSHttpTxnIntercept(icontp, txnp);

      return true;
    } else {
      DEBUG_LOG("slice passing GET or HEAD request through to next plugin");
    }
  }

  return false;
}

static int
read_resp_hdr(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn   txnp = static_cast<TSHttpTxn>(edata);
  PluginInfo *info = static_cast<PluginInfo *>(TSContDataGet(contp));

  // This function does the following things:
  // 1. Parse the object size from Content-Length
  // 2. Cache the object size
  // 3. If the object will be sliced in subsequent requests, turn off the cache to avoid taking up space, and head-of-line blocking.

  int   urllen = 0;
  char *urlstr = TSHttpTxnEffectiveUrlStringGet(txnp, &urllen);
  if (urlstr != nullptr) {
    TxnHdrMgr                response;
    TxnHdrMgr::HeaderGetFunc func = event == TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE ? TSHttpTxnCachedRespGet : TSHttpTxnServerRespGet;
    response.populateFrom(txnp, func);
    HttpHeader const resp_header(response.m_buffer, response.m_lochdr);
    char             constr[1024];
    int              conlen = sizeof constr;
    bool const hasContentLength(resp_header.valueForKey(TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH, constr, &conlen));
    if (hasContentLength) {
      uint64_t content_length;

      [[maybe_unused]] auto [ptr, ec] = std::from_chars(constr, constr + conlen, content_length);
      if (ec == std::errc()) {
        if (content_length >= info->config.m_min_size_to_slice) {
          // Remember that this object is big
          info->config.sizeCacheAdd({urlstr, static_cast<size_t>(urllen)}, content_length);
          // This object will be sliced in future requests.  Don't cache it for now.
          TSHttpTxnServerRespNoStoreSet(txnp, 1);
          TSStatIntIncrement(info->config.stat_FN, 1);
        } else {
          TSStatIntIncrement(info->config.stat_TN, 1);
        }
      } else {
        ERROR_LOG("Could not parse content-length: %.*s", conlen, constr);
        TSStatIntIncrement(info->config.stat_bad_cl, 1);
      }
    } else {
      DEBUG_LOG("Could not get a content length for updating object size");
      TSStatIntIncrement(info->config.stat_no_cl, 1);
    }
    TSfree(urlstr);
  } else {
    ERROR_LOG("Could not get URL for obj size.");
    TSStatIntIncrement(info->config.stat_no_url, 1);
  }

  // Reenable and continue with the state machine.
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

int
global_read_request_hook(TSCont // contp
                         ,
                         TSEvent // event
                         ,
                         void *edata)
{
  TSHttpTxn const txnp = static_cast<TSHttpTxn>(edata);
  read_request(txnp, &globalConfig, global_read_resp_hdr_contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // namespace

namespace slice_ns
{
DbgCtl dbg_ctl{PLUGIN_NAME};
}

///// remap plugin engine

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri ATS_UNUSED */)
{
  PluginInfo *const info = static_cast<PluginInfo *>(ih);

  if (read_request(txnp, &info->config, info->read_resp_hdr_contp)) {
    return TSREMAP_DID_REMAP_STOP;
  } else {
    return TSREMAP_NO_REMAP;
  }
}

///// remap plugin setup and teardown
void
TSRemapOSResponse(void * /* ih ATS_UNUSED */, TSHttpTxn /* rh ATS_UNUSED */, int /* os_response_type ATS_UNUSED */)
{
}

static bool
register_stat(const char *name, int &id)
{
  if (TSStatFindName(name, &id) == TS_ERROR) {
    id = TSStatCreate(name, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    if (id == TS_ERROR) {
      ERROR_LOG("Failed to register stat '%s'", name);
      return false;
    }
  }

  DEBUG_LOG("[%s] %s registered with id %d", PLUGIN_NAME, name, id);

  return true;
}

static void
init_stats(Config &config, const std::string &prefix)
{
  const std::array<std::pair<const char *, int &>, 7> stats{
    {
     {".metadata_cache.true_large_objects", config.stat_TP},
     {".metadata_cache.true_small_objects", config.stat_TN},
     {".metadata_cache.false_large_objects", config.stat_FP},
     {".metadata_cache.false_small_objects", config.stat_FN},
     {".metadata_cache.no_content_length", config.stat_no_cl},
     {".metadata_cache.bad_content_length", config.stat_bad_cl},
     {".metadata_cache.no_url", config.stat_no_url},
     }
  };

  config.stats_enabled = true;
  for (const auto &stat : stats) {
    const std::string name  = std::string{PLUGIN_NAME "."} + prefix + stat.first;
    config.stats_enabled   &= register_stat(name.c_str(), stat.second);
  }
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  PluginInfo *const info = new PluginInfo;

  info->config.fromArgs(argc - 2, argv + 2);

  TSCont read_resp_hdr_contp = TSContCreate(read_resp_hdr, nullptr);
  TSContDataSet(read_resp_hdr_contp, static_cast<void *>(info));
  info->read_resp_hdr_contp = read_resp_hdr_contp;

  if (!info->config.stat_prefix.empty()) {
    init_stats(info->config, info->config.stat_prefix);
  }

  *ih = static_cast<void *>(info);

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  if (nullptr != ih) {
    PluginInfo *const info = static_cast<PluginInfo *>(ih);
    TSContDestroy(info->read_resp_hdr_contp);
    delete info;
  }
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api_info ATS_UNUSED */, char * /* errbug ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  DEBUG_LOG("slice remap initializing.");
  return TS_SUCCESS;
}

///// global plugin
void
TSPluginInit(int argc, char const *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    ERROR_LOG("Plugin registration failed.\n");
    ERROR_LOG("Unable to initialize plugin (disabled).");
    return;
  }

  globalConfig.fromArgs(argc - 1, argv + 1);

  TSCont const global_read_request_contp(TSContCreate(global_read_request_hook, nullptr));
  global_read_resp_hdr_contp = TSContCreate(read_resp_hdr, nullptr);

  // Called immediately after the request header is read from the client
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, global_read_request_contp);

  // Register stats for metadata cache
  init_stats(globalConfig, "global");
}
