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
#include <stdlib.h>
#include <ts/ts.h>
#include <ts/remap.h>

#include "limiter.h"

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
  delete static_cast<RateLimiter *>(ih);
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  static const struct option longopt[] = {
    {const_cast<char *>("limit"), required_argument, nullptr, 'l'},
    {const_cast<char *>("queue"), required_argument, nullptr, 'q'},
    {const_cast<char *>("error"), required_argument, nullptr, 'e'},
    {const_cast<char *>("retry"), required_argument, nullptr, 'r'},
    {const_cast<char *>("header"), required_argument, nullptr, 'h'},
    {const_cast<char *>("maxage"), required_argument, nullptr, 'm'},
    // EOF
    {nullptr, no_argument, nullptr, '\0'},
  };

  RateLimiter *limiter = new RateLimiter();
  TSReleaseAssert(limiter);
  // argv contains the "to" and "from" URLs. Skip the first so that the
  // second one poses as the program name.
  --argc;
  ++argv;

  while (true) {
    int opt = getopt_long(argc, (char *const *)argv, "", longopt, nullptr);

    switch (opt) {
    case 'l':
      limiter->limit = strtol(optarg, nullptr, 10);
      break;
    case 'q':
      limiter->max_queue = strtol(optarg, nullptr, 10);
      break;
    case 'e':
      limiter->error = strtol(optarg, nullptr, 10);
      break;
    case 'r':
      limiter->retry = strtol(optarg, nullptr, 10);
      break;
    case 'm':
      limiter->max_age = std::chrono::milliseconds(strtol(optarg, nullptr, 10));
      break;
    case 'h':
      limiter->header = optarg;
      break;
    }
    if (opt == -1) {
      break;
    }
  }

  limiter->setupQueueCont();

  TSDebug(PLUGIN_NAME, "Added active_in limiter rule (limit=%u, queue=%u, error=%u", limiter->limit, limiter->max_queue,
          limiter->error);
  *ih = static_cast<void *>(limiter);

  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  RateLimiter *limiter = static_cast<RateLimiter *>(ih);

  if (limiter) {
    if (!limiter->reserve()) {
      if (!limiter->max_queue || limiter->full()) {
        // We are running at limit, and the queue has reached max capacity, give back an error and be done.
        TSHttpTxnStatusSet(txnp, static_cast<TSHttpStatus>(limiter->error));
        limiter->setupTxnCont(ih, txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK);
        TSDebug(PLUGIN_NAME, "Rejecting request, we're at capacity and queue is full");
      } else {
        limiter->setupTxnCont(ih, txnp, TS_HTTP_POST_REMAP_HOOK);
        TSDebug(PLUGIN_NAME, "Adding rate limiting hook, we are at capacity");
      }
    } else {
      limiter->setupTxnCont(ih, txnp, TS_HTTP_TXN_CLOSE_HOOK);
      TSDebug(PLUGIN_NAME, "Adding txn-close hook, we're not at capacity");
    }
  }

  return TSREMAP_NO_REMAP;
}
