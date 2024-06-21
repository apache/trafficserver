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
#include <cstdlib>
#include <cstring>

#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/remap_version.h"
#include "tscore/ink_config.h"
#include "txn_limiter.h"
#include "utilities.h"

#include "sni_selector.h"
#include "sni_limiter.h"

///////////////////////////////////////////////////////////////////////////////
// As a global plugin, things works a little different since we don't setup
// per transaction or via remap.config.
extern int gVCIdx;

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

  if (-1 == gVCIdx) {
    TSUserArgIndexReserve(TS_USER_ARGS_VCONN, PLUGIN_NAME, "VConn state information", &gVCIdx);
  }

  if (argc == 2) {
    // Make sure we start the global SNI selector before we do anything else.
    // This selector can be replaced later, during configuration reload.
    SniSelector::startup(argv[1]);
  } else {
    TSError("[%s] Usage: rate_limit.so <config.yaml>", PLUGIN_NAME);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Setup stuff for the remap plugin
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);
  Dbg(dbg_ctl, "plugin is successfully initialized");
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
  auto *limiter = new TxnRateLimiter();

  // set the name based on the pristine remap URL prior to advancing the pointer below
  limiter->setName(getDescriptionFromUrl(argv[0]));

  // argv contains the "to" and "from" URLs. Skip the first so that the
  // second one poses as the program name.
  --argc;
  ++argv;

  TSReleaseAssert(limiter);
  limiter->initialize(argc, const_cast<const char **>(argv));
  *ih = static_cast<void *>(limiter);

  if (limiter->rate() > 0) {
    // Setup rate based limit, if configured (this is rate as in "requests per second")
    limiter->addBucket();
  }

  Dbg(dbg_ctl, "Added active_in limiter rule (limit=%u, rate=%u, queue=%u, max-age=%ldms, error=%u, conntrack=%s)",
      limiter->limit(), limiter->rate(), limiter->max_queue(), static_cast<long>(limiter->max_age().count()), limiter->error(),
      limiter->conntrack() ? "yes" : "no");

  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri ATS_UNUSED */)
{
  auto *limiter = static_cast<TxnRateLimiter *>(ih);

  if (limiter) {
    TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);

    if (limiter->conntrack()) {
      int count = TSHttpSsnTransactionCount(ssnp);

      if (count > 1) { // The first transaction is the connect, so we need to have at least 2 to be "established"
        Dbg(dbg_ctl, "Allowing an established connection to pass through, txn=%d", count);
        return TSREMAP_NO_REMAP;
      }
    }

    auto status = limiter->reserve();

    switch (status) {
    case ReserveStatus::UNLIMITED:
      // No limits, just let it pass through
      break;
    case ReserveStatus::RESERVED:
      if (limiter->conntrack()) {
        limiter->setupSsnCont(ssnp);
        Dbg(dbg_ctl, "Adding ssn-close hook, we're not at capacity");
      } else {
        limiter->setupTxnCont(txnp, TS_HTTP_TXN_CLOSE_HOOK);
        Dbg(dbg_ctl, "Adding txn-close hook, we're not at capacity");
      }
      break;

    case ReserveStatus::FULL:
    case ReserveStatus::HIGH_RATE:
      if (!limiter->max_queue() || limiter->full()) {
        // We are running at limit, and the queue has reached max capacity, give back an error and be done.
        TSHttpTxnStatusSet(txnp, static_cast<TSHttpStatus>(limiter->error()));
        limiter->setupTxnCont(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK);
        Dbg(dbg_ctl, "Rejecting request, we're at %s and queue is full", status == ReserveStatus::FULL ? "capacity" : "high rate");
      } else {
        limiter->setupTxnCont(txnp, TS_HTTP_POST_REMAP_HOOK);
        Dbg(dbg_ctl, "Adding queue delay hook, we are at %s", status == ReserveStatus::FULL ? "capacity" : "high rate");
      }
      break;
    }
  }

  return TSREMAP_NO_REMAP;
}
