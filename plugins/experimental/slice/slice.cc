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

#include "ts/remap.h"
#include "ts/ts.h"

#include <netinet/in.h>
#include <mutex>

namespace
{
Config globalConfig;

bool
read_request(TSHttpTxn txnp, Config *const config)
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
        int urllen         = 0;
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

      // turn off any and all transaction caching (shouldn't matter)
      TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_SERVER_NO_STORE, true);
      TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_RESPONSE_CACHEABLE, false);
      TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_REQUEST_CACHEABLE, false);

      DEBUG_LOG("slice accepting and slicing");
      // connection back into ATS
      sockaddr const *const ip = TSHttpTxnClientAddrGet(txnp);
      if (nullptr == ip) {
        return false;
      }

      TSAssert(nullptr != config);
      Data *const data = new Data(config);

      data->m_method_type = header.method();
      data->m_txnp        = txnp;

      // set up feedback connect
      if (AF_INET == ip->sa_family) {
        memcpy(&data->m_client_ip, ip, sizeof(sockaddr_in));
      } else if (AF_INET6 == ip->sa_family) {
        memcpy(&data->m_client_ip, ip, sizeof(sockaddr_in6));
      } else {
        delete data;
        return false;
      }

      // need to reset the HOST field for global plugin
      data->m_hostlen = sizeof(data->m_hostname) - 1;
      if (!header.valueForKey(TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST, data->m_hostname, &data->m_hostlen)) {
        DEBUG_LOG("Unable to get hostname from header");
        delete data;
        return false;
      }

      // is the plugin configured to use a remap host?
      std::string const &newhost = config->m_remaphost;
      if (newhost.empty()) {
        TSMBuffer urlbuf   = nullptr;
        TSMLoc urlloc      = nullptr;
        TSReturnCode rcode = TSHttpTxnPristineUrlGet(txnp, &urlbuf, &urlloc);

        if (TS_SUCCESS == rcode) {
          TSMBuffer const newbuf = TSMBufferCreate();
          TSMLoc newloc          = nullptr;
          rcode                  = TSUrlClone(newbuf, urlbuf, urlloc, &newloc);
          TSHandleMLocRelease(urlbuf, TS_NULL_MLOC, urlloc);

          if (TS_SUCCESS != rcode) {
            ERROR_LOG("Error cloning pristine url");
            TSMBufferDestroy(newbuf);
            delete data;
            return false;
          }

          data->m_urlbuf = newbuf;
          data->m_urlloc = newloc;
        }
      } else { // grab the effective url, swap out the host and zero the port
        int len            = 0;
        char *const effstr = TSHttpTxnEffectiveUrlStringGet(txnp, &len);

        if (nullptr != effstr) {
          TSMBuffer const newbuf = TSMBufferCreate();
          TSMLoc newloc          = nullptr;
          bool okay              = false;

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
            delete data;
            return false;
          }

          data->m_urlbuf = newbuf;
          data->m_urlloc = newloc;
        }
      }

      data->m_buffer_index      = TSPluginVCIOBufferIndexGet(data->m_txnp);     // default of m_buffer_index = 32KB
      data->m_buffer_water_mark = TSPluginVCIOBufferWaterMarkGet(data->m_txnp); // default of m_buffer_water_mark = 0

      if (TSIsDebugTagSet(PLUGIN_NAME)) {
        int len            = 0;
        char *const urlstr = TSUrlStringGet(data->m_urlbuf, data->m_urlloc, &len);
        DEBUG_LOG("slice url: %.*s", len, urlstr);
        TSfree(urlstr);
      }

      // we'll intercept this GET and do it ourselves
      TSMutex const mutex = TSContMutexGet(reinterpret_cast<TSCont>(txnp));
      TSCont const icontp(TSContCreate(intercept_hook, mutex));
      TSContDataSet(icontp, (void *)data);

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

int
global_read_request_hook(TSCont // contp
                         ,
                         TSEvent // event
                         ,
                         void *edata)
{
  TSHttpTxn const txnp = static_cast<TSHttpTxn>(edata);
  read_request(txnp, &globalConfig);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // namespace

///// remap plugin engine

SLICE_EXPORT
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  Config *const config = static_cast<Config *>(ih);

  if (read_request(txnp, config)) {
    return TSREMAP_DID_REMAP_STOP;
  } else {
    return TSREMAP_NO_REMAP;
  }
}

///// remap plugin setup and teardown
SLICE_EXPORT
void
TSRemapOSResponse(void *ih, TSHttpTxn rh, int os_response_type)
{
}

SLICE_EXPORT
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  Config *const config = new Config;
  config->fromArgs(argc - 2, argv + 2);
  *ih = static_cast<void *>(config);
  return TS_SUCCESS;
}

SLICE_EXPORT
void
TSRemapDeleteInstance(void *ih)
{
  if (nullptr != ih) {
    Config *const config = static_cast<Config *>(ih);
    delete config;
  }
}

SLICE_EXPORT
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbug, int errbuf_size)
{
  DEBUG_LOG("slice remap initializing.");
  return TS_SUCCESS;
}

///// global plugin
SLICE_EXPORT
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

  TSCont const contp(TSContCreate(global_read_request_hook, nullptr));

  // Called immediately after the request header is read from the client
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
}
