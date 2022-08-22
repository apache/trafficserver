/** @file

  This plugin counts the number of times every header has appeared.
  Maintains separate counts for client and origin headers.

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

#include <iostream>
#include <map>
#include <memory>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/parentselectdefs.h"

#include "consistenthash_config.h"
#include "strategy.h"

// TODO summary:
// - TSRemapInit version check

namespace
{
// The strategy and its transaction state.
struct StrategyTxn {
  TSNextHopSelectionStrategy *strategy;
  void *txn; // void* because the actual type will depend on the strategy.
  int request_count;
  TSResponseAction prev_ra;
};

// mark parents up or down, on failure or successful retry.
void
mark_response(TSHttpTxn txnp, StrategyTxn *strategyTxn, TSHttpStatus status)
{
  TSDebug(PLUGIN_NAME, "mark_response calling with code: %d", status);

  auto strategy = strategyTxn->strategy;

  const bool isFailure = strategy->codeIsFailure(status);

  TSResponseAction ra;
  // if the prev_host isn't null, then that was the actual host we tried which needs to be marked down.
  if (strategyTxn->prev_ra.hostname_len != 0) {
    ra = strategyTxn->prev_ra;
    TSDebug(PLUGIN_NAME, "mark_response using prev %.*s:%d", int(ra.hostname_len), ra.hostname, ra.port);
  } else {
    TSHttpTxnResponseActionGet(txnp, &ra);
    TSDebug(PLUGIN_NAME, "mark_response using response_action %.*s:%d", int(ra.hostname_len), ra.hostname, ra.port);
  }

  if (isFailure && strategy->onFailureMarkParentDown(status)) {
    if (ra.hostname == nullptr) {
      TSError(
        "[%s] mark_response got a failure, but response_action had no hostname! This shouldn't be possible! Not marking down!",
        PLUGIN_NAME);
    } else {
      TSDebug(PLUGIN_NAME, "mark_response marking %.*s:%d down", int(ra.hostname_len), ra.hostname, ra.port);
      strategy->mark(txnp, strategyTxn->txn, ra.hostname, ra.hostname_len, ra.port, PL_NH_MARK_DOWN);
    }
  } else if (!isFailure && ra.is_retry) {
    if (ra.hostname == nullptr) {
      TSError(
        "[%s] mark_response got a retry success, but response_action had no hostname! This shouldn't be possible! Not marking up!",
        PLUGIN_NAME);
    } else {
      TSDebug(PLUGIN_NAME, "mark_response marking %.*s:%d up", int(ra.hostname_len), ra.hostname, ra.port);
      strategy->mark(txnp, strategyTxn->txn, ra.hostname, ra.hostname_len, ra.port, PL_NH_MARK_UP);
    }
  }
}

int
handle_read_response(TSHttpTxn txnp, StrategyTxn *strategyTxn)
{
  TSDebug(PLUGIN_NAME, "handle_read_response calling");

  auto strategy = strategyTxn->strategy;

  TSDebug(PLUGIN_NAME, "handle_read_response got strategy '%s'", strategy->name());

  TSMBuffer resp;
  TSMLoc resp_hdr;
  if (TS_SUCCESS != TSHttpTxnServerRespGet(txnp, &resp, &resp_hdr)) {
    TSDebug(PLUGIN_NAME, "handle_read_response failed to get resp");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  TSHttpStatus status = TSHttpHdrStatusGet(resp, resp_hdr);
  TSDebug(PLUGIN_NAME, "handle_read_response got response code: %d", status);

  mark_response(txnp, strategyTxn, status);

  if (!strategy->codeIsFailure(status)) {
    // if it's a success, set the action to not retry
    TSResponseAction ra;
    // this sets failed=false, responseIsRetryable=false => don't retry, return the success
    memset(&ra, 0, sizeof(TSResponseAction)); // because {0} gives a C++ warning. Ugh.
    TSDebug(PLUGIN_NAME, "handle_read_response success, setting response_action to not retry");
    TSHttpTxnResponseActionSet(txnp, &ra);
  } else {
    // We already set the response_action for what to do on failure in send_request.
    // (because we don't always get here with responses, like DNS or connection failures)
    // But we need to get the action previously set, and update responseIsRetryable, which we couldn't determine before without the
    // Status.
    TSResponseAction ra;
    TSHttpTxnResponseActionGet(txnp, &ra);
    ra.responseIsRetryable = strategy->responseIsRetryable(strategyTxn->request_count - 1, status);

    TSHttpTxnResponseActionSet(txnp, &ra);
  }

  // un-set the "prev" hackery. That only exists for markdown, which we just did.
  // The response_action is now the next thing to try, if this was a failure,
  // and should now be considered authoritative for everything.

  memset(&strategyTxn->prev_ra, 0, sizeof(TSResponseAction));

  TSHandleMLocRelease(resp, TS_NULL_MLOC, resp_hdr);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_os_dns(TSHttpTxn txnp, StrategyTxn *strategyTxn)
{
  TSDebug(PLUGIN_NAME, "handle_os_dns calling");

  ++strategyTxn->request_count;

  auto strategy = strategyTxn->strategy;

  TSDebug(PLUGIN_NAME, "handle_os_dns got strategy '%s'", strategy->name());

  const TSServerState server_state = TSHttpTxnServerStateGet(txnp);
  if (server_state == TS_SRVSTATE_CONNECTION_ERROR || server_state == TS_SRVSTATE_INACTIVE_TIMEOUT) {
    mark_response(txnp, strategyTxn, STATUS_CONNECTION_FAILURE);
  }

  TSDebug(PLUGIN_NAME, "handle_os_dns had no prev, setting new response_action");

  {
    TSResponseAction ra;
    TSHttpTxnResponseActionGet(txnp, &ra);
    strategyTxn->prev_ra = ra;
  }

  TSResponseAction ra;
  memset(&ra, 0, sizeof(TSResponseAction));
  strategy->next(txnp, strategyTxn->txn, &ra.hostname, &ra.hostname_len, &ra.port, &ra.is_retry, &ra.no_cache);

  ra.fail = ra.hostname == nullptr; // failed is whether to immediately fail and return the client a 502. In this case: whether or
                                    // not we found another parent.
  ra.nextHopExists       = ra.hostname_len != 0;
  ra.responseIsRetryable = strategy->responseIsRetryable(strategyTxn->request_count - 1, STATUS_CONNECTION_FAILURE);
  ra.goDirect            = strategy->goDirect();
  ra.parentIsProxy       = strategy->parentIsProxy();
  TSDebug(PLUGIN_NAME, "handle_os_dns setting response_action hostname '%.*s' port %d direct %d proxy %d is_retry %d exists %d",
          int(ra.hostname_len), ra.hostname, ra.port, ra.goDirect, ra.parentIsProxy, ra.is_retry, ra.nextHopExists);
  TSHttpTxnResponseActionSet(txnp, &ra);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_txn_close(TSHttpTxn txnp, TSCont contp, StrategyTxn *strategyTxn)
{
  TSDebug(PLUGIN_NAME, "handle_txn_close calling");

  auto strategy = strategyTxn->strategy;

  if (strategy != nullptr) {
    TSContDataSet(contp, nullptr);
    strategy->deleteTxn(strategyTxn->txn);
    delete strategyTxn;
    // we delete the state, and the strategyAndState pointer at the end of the transaction,
    // but we DON'T delete the Strategy, which lives as long as the remap.
  }
  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_hook(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "handle_hook calling");

  TSHttpTxn txnp           = static_cast<TSHttpTxn>(edata);
  StrategyTxn *strategyTxn = static_cast<StrategyTxn *>(TSContDataGet(contp));

  TSDebug(PLUGIN_NAME, "handle_hook got strategy '%s'", strategyTxn->strategy->name());

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    return handle_read_response(txnp, strategyTxn);
  case TS_EVENT_HTTP_OS_DNS:
    return handle_os_dns(txnp, strategyTxn);
  case TS_EVENT_HTTP_TXN_CLOSE:
    return handle_txn_close(txnp, contp, strategyTxn);
  default:
    TSError("[%s] handle_hook got unknown event %d - should never happen!", PLUGIN_NAME, event);
    return TS_ERROR;
  }
}

} // namespace

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "TSRemapInit calling");

  // TODO add ATS API Version check here, to bail if ATS doesn't support the version necessary for strategy plugins

  if (!api_info) {
    strncpy(errbuf, "[tsstrategy_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSStrategyInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "Remap successfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuff, int errbuff_size)
{
  TSDebug(PLUGIN_NAME, "TSRemapNewInstance calling");

  *ih = nullptr;

  for (int i = 0; i < argc; ++i) {
    TSDebug(PLUGIN_NAME, "TSRemapNewInstance arg %d '%s'", i, argv[i]);
  }

  if (argc < 4) {
    TSError("[%s] insufficient number of arguments, %d, expected file and strategy argument.", PLUGIN_NAME, argc);
    return TS_ERROR;
  }
  if (argc > 4) {
    TSError("[%s] too many arguments, %d, only expected file and strategy argument.", PLUGIN_NAME, argc);
    return TS_ERROR;
  }

  const char *remap_from       = argv[0];
  const char *remap_to         = argv[1];
  const char *config_file_path = argv[2];
  const char *strategy_name    = argv[3];

  TSDebug(PLUGIN_NAME, "%s %s Loading parent selection strategy file %s for strategy %s", remap_from, remap_to, config_file_path,
          strategy_name);
  auto file_strategies = createStrategiesFromFile(config_file_path);
  if (file_strategies.size() == 0) {
    TSError("[%s] %s %s Failed to parse configuration file %s", PLUGIN_NAME, remap_from, remap_to, config_file_path);
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "'%s' '%s' successfully created strategies in file %s num %d", remap_from, remap_to, config_file_path,
          int(file_strategies.size()));

  auto new_strategy = file_strategies.find(strategy_name);
  if (new_strategy == file_strategies.end()) {
    TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance strategy '%s' not found in file '%s'", remap_from, remap_to, strategy_name,
            config_file_path);
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance successfully loaded strategy '%s' from '%s'.", remap_from, remap_to,
          strategy_name, config_file_path);

  // created a raw pointer _to_ a shared_ptr, because ih needs a raw pointer.
  // The raw pointer in ih will be deleted in TSRemapDeleteInstance,
  // which will destruct the shared_ptr,
  // destroying the strategy if this is the last remap rule using it.
  *ih = static_cast<void *>(new std::shared_ptr<TSNextHopSelectionStrategy>(new_strategy->second));

  // Associate our config file with remap.config to be able to initiate reloads
  TSMgmtString result;
  const char *var_name = "proxy.config.url_remap.filename";
  TSMgmtStringGet(var_name, &result);
  TSMgmtConfigFileAdd(result, config_file_path);

  return TS_SUCCESS;
}

extern "C" tsapi TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  TSDebug(PLUGIN_NAME, "TSRemapDoRemap calling");

  auto strategy_ptr = static_cast<std::shared_ptr<TSNextHopSelectionStrategy> *>(ih);
  auto strategy     = strategy_ptr->get();

  TSDebug(PLUGIN_NAME, "TSRemapDoRemap got strategy '%s'", strategy->name());

  TSCont cont = TSContCreate(handle_hook, TSMutexCreate());

  auto strategyTxn           = new StrategyTxn();
  strategyTxn->strategy      = strategy;
  strategyTxn->txn           = strategy->newTxn();
  strategyTxn->request_count = 0;
  memset(&strategyTxn->prev_ra, 0, sizeof(TSResponseAction));
  TSContDataSet(cont, (void *)strategyTxn);

  TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_OS_DNS_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, cont);

  TSResponseAction ra;
  memset(&ra, 0, sizeof(TSResponseAction)); // because {0} gives a C++ warning. Ugh.
  strategy->next(txnp, strategyTxn->txn, &ra.hostname, &ra.hostname_len, &ra.port, &ra.is_retry, &ra.no_cache);

  ra.nextHopExists = ra.hostname != nullptr;
  ra.fail          = !ra.nextHopExists;
  // The action here is used for the very first connection, not any retry. So of course we should try it.
  ra.responseIsRetryable = true;
  ra.goDirect            = strategy->goDirect();
  ra.parentIsProxy       = strategy->parentIsProxy();

  if (ra.fail && !ra.goDirect) {
    // TODO make configurable
    TSDebug(PLUGIN_NAME, "TSRemapDoRemap strategy '%s' next returned nil, returning 502!", strategy->name());
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_BAD_GATEWAY);
    // TODO verify TS_EVENT_HTTP_TXN_CLOSE fires, and if not, free the cont here.
    return TSREMAP_DID_REMAP;
  }

  TSDebug(PLUGIN_NAME, "TSRemapDoRemap setting response_action hostname '%.*s' port %d direct %d proxy %d", int(ra.hostname_len),
          ra.hostname, ra.port, ra.goDirect, ra.parentIsProxy);
  TSHttpTxnResponseActionSet(txnp, &ra);

  return TSREMAP_NO_REMAP;
}

extern "C" tsapi void
TSRemapDeleteInstance(void *ih)
{
  TSDebug(PLUGIN_NAME, "TSRemapDeleteInstance calling");
  auto strategy_ptr = static_cast<std::shared_ptr<TSNextHopSelectionStrategy> *>(ih);
  delete strategy_ptr;
  TSDebug(PLUGIN_NAME, "TSRemapDeleteInstance deleted strategy pointer");
}

void
TSRemapPreConfigReload(void)
{
  TSDebug(PLUGIN_NAME, "TSRemapPreConfigReload clearing strategies cache");
  clearStrategiesCache();
}
