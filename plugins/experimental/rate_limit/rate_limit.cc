/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unistd.h>
#include <getopt.h>
#include <cstdlib>
#include <ts/ts.h>
#include <ts/remap.h>

#include <openssl/ssl.h>

#include "txn_limiter.h"
#include "utilities.h"

///////////////////////////////////////////////////////////////////////////////
// Initialize the InkAPI plugin for the global hooks we support.
//
static int
globalSNICont(TSCont contp, TSEvent event, void *edata)
{
  TxnRateLimiter *limiter = static_cast<TxnRateLimiter *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_SSL_CLIENT_HELLO: {
    auto vc                   = static_cast<TSVConn>(edata);
    TSSslConnection ssl_conn  = TSVConnSslConnectionGet(vc);
    SSL *ssl                  = reinterpret_cast<SSL *>(ssl_conn);
    std::string_view sni_name = getSNI(ssl);

    TSDebug(PLUGIN_NAME, "CLIENT_HELLO on %.*s", static_cast<int>(sni_name.length()), sni_name.data());

    if (!limiter->reserve()) {
      if (!limiter->max_queue || limiter->full()) {
        // We are running at limit, and the queue has reached max capacity, give back an error and be done.
        TSVConnReenableEx(vc, TS_EVENT_ERROR);
        TSDebug(PLUGIN_NAME, "Rejecting connection, we're at capacity and queue is full");

        return TS_ERROR;
      } else {
        // ToDo: queue the VC here, do not re-enable
        TSDebug(PLUGIN_NAME, "Queueing the VC, we are at capacity");
      }
    } else {
      // Not at limit on the handshake, we can re-enable
      TSVConnReenable(vc);
    }
    break;
  }
  case TS_EVENT_HTTP_SSN_CLOSE:
    limiter->release();
    TSHttpSsnReenable(static_cast<TSHttpSsn>(edata), TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    TSDebug(PLUGIN_NAME, "Unknown event %d", static_cast<int>(event));
    TSError("Unknown event in %s", PLUGIN_NAME);
    break;
  }

  return TS_EVENT_CONTINUE;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }

  if (argc > 1) {
    if (!strcasecmp(argv[1], "SNI")) {
      TSCont sni_cont         = TSContCreate(globalSNICont, nullptr);
      TxnRateLimiter *limiter = new TxnRateLimiter();

      --argc; // Skip the "SNI" portion of course when parsing the real parameters
      ++argv;

      TSReleaseAssert(sni_cont);
      TSReleaseAssert(limiter);

      limiter->initialize(argc, argv);
      TSContDataSet(sni_cont, limiter);
      TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, sni_cont);
      TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, sni_cont);

      // TSHttpHookAdd(TS_HTTP_PRE_REMAP_HOOK, host_cont);
      // TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, sni_cont);

      TSDebug(PLUGIN_NAME, "Added global active_in limiter rule (limit=%u, queue=%u, error=%u", limiter->limit, limiter->max_queue,
              limiter->error);
    } else if (!strcasecmp(argv[1], "HOST")) {
      // TODO: Do we need to implement this ?? Or can we just defer this to the remap version?
      --argc; // Skip the "HOST" arg of course when parsing the real parameters
      ++argv;
      // TSCont host_cont = TSContCreate(globalHostCont, nullptr);
    } else {
      TSError("[%s] unknown global limiter type: %s", PLUGIN_NAME, argv[1]);
    }
  } else {
    TSError("[%s] Usage: rate_limit.so SNI|HOST [option arguments]", PLUGIN_NAME);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Setup stuff for the remap plugin
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "plugin is successfully initialized");
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  delete static_cast<TxnRateLimiter *>(ih);
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  TxnRateLimiter *limiter = new TxnRateLimiter();

  // argv contains the "to" and "from" URLs. Skip the first so that the
  // second one poses as the program name.
  --argc;
  ++argv;

  TSReleaseAssert(limiter);
  limiter->initialize(argc, const_cast<const char **>(argv));
  *ih = static_cast<void *>(limiter);

  TSDebug(PLUGIN_NAME, "Added active_in limiter rule (limit=%u, queue=%u, error=%u", limiter->limit, limiter->max_queue,
          limiter->error);

  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  TxnRateLimiter *limiter = static_cast<TxnRateLimiter *>(ih);

  if (limiter) {
    if (!limiter->reserve()) {
      if (!limiter->max_queue || limiter->full()) {
        // We are running at limit, and the queue has reached max capacity, give back an error and be done.
        TSHttpTxnStatusSet(txnp, static_cast<TSHttpStatus>(limiter->error));
        limiter->setupCont(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK);
        TSDebug(PLUGIN_NAME, "Rejecting request, we're at capacity and queue is full");
      } else {
        limiter->setupCont(txnp, TS_HTTP_POST_REMAP_HOOK);
        TSDebug(PLUGIN_NAME, "Adding rate limiting hook, we are at capacity");
      }
    } else {
      limiter->setupCont(txnp, TS_HTTP_TXN_CLOSE_HOOK);
      TSDebug(PLUGIN_NAME, "Adding txn-close hook, we're not at capacity");
    }
  }

  return TSREMAP_NO_REMAP;
}
