/** @file

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

#include "tscore/ink_config.h"
#include "tscore/ink_defs.h"
#include "tscore/BufferWriter.h"

#include "ts/ts.h"

#include <string>
#include <string_view>
#include <unordered_map>

#define PLUGIN_NAME "remap_stats"
#define DEBUG_TAG PLUGIN_NAME

#define MAX_STAT_LENGTH (1 << 8)

enum UriType {
  REMAP,
  PRISTINE,
};

struct config_t {
  TSMutex stat_creation_mutex{nullptr};
  UriType uri_type{PRISTINE};
  TSStatPersistence persist_type{TS_STAT_NON_PERSISTENT};
  int txn_slot{-1};
};

// From "core".... sigh, but we need it for now at least.
extern int max_records_entries;

namespace
{
void
stat_add(const char *name, TSMgmtInt amount, TSStatPersistence persist_type, TSMutex create_mutex)
{
  static thread_local std::unordered_map<std::string, int> hash;
  int stat_id = -1;

  if (unlikely(hash.find(name) == hash.cend())) {
    // This is an unlikely path because we most likely have the stat cached
    // so this mutex won't be much overhead and it fixes a race condition
    // in the RecCore. Hopefully this can be removed in the future.
    TSMutexLock(create_mutex);
    if (TS_ERROR == TSStatFindName(name, &stat_id)) {
      stat_id = TSStatCreate(name, TS_RECORDDATATYPE_INT, persist_type, TS_STAT_SYNC_SUM);
      if (stat_id == TS_ERROR) {
        TSDebug(DEBUG_TAG, "Error creating stat_name: %s", name);
      } else {
        TSDebug(DEBUG_TAG, "Created stat_name: %s stat_id: %d", name, stat_id);
      }
    }
    TSMutexUnlock(create_mutex);

    if (stat_id >= 0) {
      hash.emplace(name, stat_id);
      TSDebug(DEBUG_TAG, "Cached stat_name: %s stat_id: %d", name, stat_id);
    }
  } else {
    stat_id = hash.at(name);
  }

  if (likely(stat_id >= 0)) {
    TSStatIntIncrement(stat_id, amount);
  } else {
    TSDebug(DEBUG_TAG, "stat error! stat_name: %s stat_id: %d", name, stat_id);
  }
}

std::string
get_hostname(TSHttpTxn txnp, UriType uriType)
{
  std::string hostname;

  switch (uriType) {
  case PRISTINE: {
    TSMBuffer hbuf;
    TSMLoc hloc;
    if (TS_SUCCESS == TSHttpTxnPristineUrlGet(txnp, &hbuf, &hloc)) {
      int tlen                = 0;
      char const *const thost = TSUrlHostGet(hbuf, hloc, &tlen);
      if (nullptr != thost && 0 < tlen) {
        hostname.assign(thost, tlen);
      }
      TSHandleMLocRelease(hbuf, TS_NULL_MLOC, hloc);
    }
  } break;
  case REMAP: {
    TSMBuffer hbuf;
    TSMLoc hloc;
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &hbuf, &hloc)) {
      TSMLoc url_loc;
      if (TS_SUCCESS == TSHttpHdrUrlGet(hbuf, hloc, &url_loc)) {
        int tlen                = 0;
        char const *const thost = TSUrlHostGet(hbuf, url_loc, &tlen);
        if (nullptr != thost && 0 < tlen) {
          hostname.assign(thost, tlen);
        }
        TSHandleMLocRelease(hbuf, hloc, url_loc);
      }
      TSHandleMLocRelease(hbuf, TS_NULL_MLOC, hloc);
    }
  } break;
  default:
    break;
  }

  return hostname;
}

int
handle_post_remap(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
{
  TSHttpTxn txn          = static_cast<TSHttpTxn>(edata);
  config_t *const config = static_cast<config_t *>(TSContDataGet(cont));
  void *const txnd       = reinterpret_cast<void *>(0x01);
  TSUserArgSet(txn, config->txn_slot, txnd);
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  TSDebug(DEBUG_TAG, "Post Remap Handler Finished");
  return 0;
}

void
create_stat_name(ts::FixedBufferWriter &stat_name, std::string_view const h, std::string_view const b)
{
  stat_name.reset().clip(1);
  stat_name.print("plugin.{}.{}.{}", PLUGIN_NAME, h, b);
  stat_name.extend(1).write('\0');
}

int
handle_txn_close(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
{
  TSHttpTxn const txn          = static_cast<TSHttpTxn>(edata);
  config_t const *const config = static_cast<config_t *>(TSContDataGet(cont));
  void const *const txnd       = TSUserArgGet(txn, config->txn_slot);

  static std::string_view const unknown = "unknown";

  // check remap successful
  if (nullptr != txnd) {
    std::string const hostname = get_hostname(txn, config->uri_type);

    std::string_view hostsv;
    if (!hostname.empty()) {
      hostsv = hostname;
    } else {
      hostsv = unknown;
    }

    uint64_t in_bytes = TSHttpTxnClientReqHdrBytesGet(txn);
    in_bytes += TSHttpTxnClientReqBodyBytesGet(txn);

    ts::LocalBufferWriter<MAX_STAT_LENGTH> stat_name;

    create_stat_name(stat_name, hostsv, "in_bytes");
    stat_add(stat_name.data(), static_cast<TSMgmtInt>(in_bytes), config->persist_type, config->stat_creation_mutex);

    uint64_t out_bytes = TSHttpTxnClientRespHdrBytesGet(txn);
    out_bytes += TSHttpTxnClientRespBodyBytesGet(txn);

    create_stat_name(stat_name, hostsv, "out_bytes");
    stat_add(stat_name.data(), static_cast<TSMgmtInt>(out_bytes), config->persist_type, config->stat_creation_mutex);

    TSMBuffer buf  = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientRespGet(txn, &buf, &hdr_loc) == TS_SUCCESS) {
      int const status_code = static_cast<int>(TSHttpHdrStatusGet(buf, hdr_loc));
      TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);

      if (status_code < 200) {
        create_stat_name(stat_name, hostsv, "status_other");
      } else if (status_code <= 299) {
        create_stat_name(stat_name, hostsv, "status_2xx");
      } else if (status_code <= 399) {
        create_stat_name(stat_name, hostsv, "status_3xx");
      } else if (status_code <= 499) {
        create_stat_name(stat_name, hostsv, "status_4xx");
      } else if (status_code <= 599) {
        create_stat_name(stat_name, hostsv, "status_5xx");
      } else {
        create_stat_name(stat_name, hostsv, "status_other");
      }

      stat_add(stat_name.data(), 1, config->persist_type, config->stat_creation_mutex);
    } else {
      create_stat_name(stat_name, hostsv, "status_unknown");
      stat_add(stat_name.data(), 1, config->persist_type, config->stat_creation_mutex);
    }
  } else {
    TSDebug(DEBUG_TAG, "skipping unsuccessfully remapped transaction");
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  TSDebug(DEBUG_TAG, "Handler Finished");
  return 0;
}

} // namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[remap_stats] Plugin registration failed");

    return;
  } else {
    TSDebug(DEBUG_TAG, "Plugin registration succeeded");
  }

  auto config                 = new config_t;
  config->stat_creation_mutex = TSMutexCreate();
  config->uri_type            = PRISTINE;
  config->persist_type        = TS_STAT_NON_PERSISTENT;

  if (argc > 1) {
    // Argument parser
    for (int ii = 0; ii < argc; ++ii) {
      std::string_view const arg(argv[ii]);
      if (arg == "-P" || arg == "--post-remap-host") {
        config->uri_type = REMAP;
        TSDebug(DEBUG_TAG, "Using post remap hostname");
      } else if (arg == "-p" || arg == "--persistent") {
        config->persist_type = TS_STAT_PERSISTENT;
        TSDebug(DEBUG_TAG, "Using persistent stats");
      }
    }
  }

  TSUserArgIndexReserve(TS_USER_ARGS_TXN, PLUGIN_NAME, "txn data", &(config->txn_slot));

  // this is to mark the transaction as successfully remapped
  TSCont const post_remap_cont = TSContCreate(handle_post_remap, nullptr);
  TSContDataSet(post_remap_cont, static_cast<void *>(config));
  TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, post_remap_cont);

  // collects stats for successful remaps
  TSCont const global_cont = TSContCreate(handle_txn_close, nullptr);
  TSContDataSet(global_cont, static_cast<void *>(config));
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, global_cont);

  TSDebug(DEBUG_TAG, "Init complete");
}
