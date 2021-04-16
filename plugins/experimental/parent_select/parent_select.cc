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
  const char *prev_host; // the actually tried host, used when send_request sets the response_action to be the next thing to try.
  size_t prev_host_len;
  in_port_t prev_port;
  bool prev_is_retry;
};

int
handle_send_request(TSHttpTxn txnp, StrategyTxn *strategyTxn)
{
  TSDebug(PLUGIN_NAME, "handle_send_request calling");
  TSDebug(PLUGIN_NAME, "handle_send_request got strategy '%s'", strategyTxn->strategy->name());

  auto strategy = strategyTxn->strategy;

  // if (strategyTxn->retry_attempts == 0) {
  //   // just did a DoRemap, which means we need to set the response_action of what to do in the event of failure
  //   // because a failure might not call read_response (e.g. dns failure)
  //   strategyTxn->retry_attempts = 1;
  //   TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  //   return TS_SUCCESS;
  // }

  // before sending a req, we need to set what to do on failure.
  // Because some failures don't call handle_response before getting to HttpTransact::HandleResponse
  // (e.g. connection failures)

  TSResponseAction ra;
  TSHttpTxnResponseActionGet(txnp, &ra);

  TSDebug(PLUGIN_NAME, "handle_send_request setting prev %.*s:%d", int(ra.hostname_len), ra.hostname, ra.port);
  strategyTxn->prev_host     = ra.hostname;
  strategyTxn->prev_host_len = ra.hostname_len;
  strategyTxn->prev_port     = ra.port;
  strategyTxn->prev_is_retry = ra.is_retry;

  strategy->next(txnp, strategyTxn->txn, ra.hostname, ra.hostname_len, ra.port, &ra.hostname, &ra.hostname_len, &ra.port,
                 &ra.is_retry);

  ra.nextHopExists = strategy->nextHopExists(txnp);
  ra.fail = !ra.nextHopExists; // failed is whether to fail and return to the client. failed=false means to retry the parent we set
                               // in the response_action

  // we don't know if it's retryable yet, because we don't have a status. So set it retryable if we have something which could be
  // retried. We'll set it retryable per the status in handle_response, and os_dns (which is called on connection failures, and
  // always retryable [notwithstanding num_retries]).
  ra.responseIsRetryable = ra.nextHopExists;
  ra.goDirect            = strategy->goDirect();
  ra.parentIsProxy       = strategy->parentIsProxy();

  TSDebug(PLUGIN_NAME, "handle_send_request setting response_action hostname '%.*s' port %d direct %d proxy %d",
          int(ra.hostname_len), ra.hostname, ra.port, ra.goDirect, ra.parentIsProxy);
  TSHttpTxnResponseActionSet(txnp, &ra);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

// mark parents up or down, on failure or successful retry.
void
mark_response(TSHttpTxn txnp, StrategyTxn *strategyTxn, TSHttpStatus status)
{
  TSDebug(PLUGIN_NAME, "mark_response calling with code: %d", status);

  auto strategy = strategyTxn->strategy;

  const bool isFailure = strategy->codeIsFailure(status);

  TSResponseAction ra;
  // if the prev_host isn't null, then that was the actual host we tried which needs to be marked down.
  if (strategyTxn->prev_host != nullptr) {
    ra.hostname     = strategyTxn->prev_host;
    ra.hostname_len = strategyTxn->prev_host_len;
    ra.port         = strategyTxn->prev_port;
    ra.is_retry     = strategyTxn->prev_is_retry;
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

  // increment request count here, not send_request, because we need to consistently increase with os_dns hooks.
  // if we incremented the req count in send_request and not here, that would never be called on DNS failures, but DNS successes
  // would call os_dns and also send_request, resulting in dns failures incrementing the count by 1, and dns successes but http
  // failures would increment by 2. And successes would increment by 2. Hence, the only consistent way to count requests is on
  // read_response and os_dns, and not send_request.
  ++strategyTxn->request_count;

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
    ra.responseIsRetryable = strategy->responseIsRetryable(strategyTxn->request_count, status);
    TSHttpTxnResponseActionSet(txnp, &ra);
  }

  // un-set the "prev" hackery. That only exists for markdown, which we just did.
  // The response_action is now the next thing to try, if this was a failure,
  // and should now be considered authoritative for everything.
  strategyTxn->prev_host     = nullptr;
  strategyTxn->prev_host_len = 0;
  strategyTxn->prev_port     = 0;
  strategyTxn->prev_is_retry = false;

  TSHandleMLocRelease(resp, TS_NULL_MLOC, resp_hdr);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_os_dns(TSHttpTxn txnp, StrategyTxn *strategyTxn)
{
  TSDebug(PLUGIN_NAME, "handle_os_dns calling");

  ++strategyTxn->request_count; // this is called after connection failures. So if we got here, we attempted a request

  // This is not called on the first attempt.
  // Thus, if we got called here, we know it's because of a parent failure.
  // So immediately find the next parent, and set the response_action.

  auto strategy = strategyTxn->strategy;

  TSDebug(PLUGIN_NAME, "handle_os_dns got strategy '%s'", strategy->name());

  mark_response(txnp, strategyTxn, STATUS_CONNECTION_FAILURE);

  // now, we need to figure out, are we the first call after send_response set the response_action as the next-thing-to-try,
  // or are we a subsequent call, and need to actually set a new response_action

  if (strategyTxn->prev_host != nullptr) {
    TSDebug(PLUGIN_NAME, "handle_os_dns had prev, keeping existing response_action and un-setting prev");
    // if strategyTxn->prev_host exists, this is the very first call after send_response set the response_action to the next thing
    // to try. and no handle_response was called in-between (because it was a connection or dns failure) So keep that, and set
    // prev_host=nullptr (so we get a new response_action the next time we're called)
    strategyTxn->prev_host     = nullptr;
    strategyTxn->prev_port     = 0;
    strategyTxn->prev_is_retry = false;
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  TSDebug(PLUGIN_NAME, "handle_os_dns had no prev, setting new response_action");

  TSResponseAction ra;
  memset(&ra, 0, sizeof(TSResponseAction)); // because {0} gives a C++ warning. Ugh.
  const char *const exclude_host = nullptr;
  const size_t exclude_host_len  = 0;
  const in_port_t exclude_port   = 0;
  strategy->next(txnp, strategyTxn->txn, exclude_host, exclude_host_len, exclude_port, &ra.hostname, &ra.hostname_len, &ra.port,
                 &ra.is_retry);

  ra.fail = ra.hostname == nullptr; // failed is whether to immediately fail and return the client a 502. In this case: whether or
                                    // not we found another parent.
  ra.nextHopExists       = strategy->nextHopExists(txnp);
  ra.responseIsRetryable = strategy->responseIsRetryable(strategyTxn->request_count, STATUS_CONNECTION_FAILURE);
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
  // case TS_EVENT_HTTP_READ_REQUEST_HDR:
  //   return handle_read_request(txnp, strategyTxn);
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    return handle_send_request(txnp, strategyTxn);
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    return handle_read_response(txnp, strategyTxn);
  // case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
  //   return handle_send_response(txnp, strategyTxn);
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

  std::unique_ptr<TSNextHopSelectionStrategy> new_strategy;

  for (auto &[name, strategy] : file_strategies) {
    TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance strategy file had strategy named '%s'", remap_from, remap_to, name.c_str());
    if (strncmp(strategy_name, name.c_str(), strlen(strategy_name)) != 0) {
      continue;
    }
    TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance using '%s'", remap_from, remap_to, name.c_str());
    new_strategy = std::move(strategy);
  }

  if (new_strategy.get() == nullptr) {
    TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance strategy '%s' not found in file '%s'", remap_from, remap_to, strategy_name,
            config_file_path);
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance successfully loaded strategy '%s' from '%s'.", remap_from, remap_to,
          strategy_name, config_file_path);
  *ih = static_cast<void *>(new_strategy.release());
  return TS_SUCCESS;
}

extern "C" tsapi TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  TSDebug(PLUGIN_NAME, "TSRemapDoRemap calling");

  auto strategy = static_cast<TSNextHopSelectionStrategy *>(ih);

  TSDebug(PLUGIN_NAME, "TSRemapDoRemap got strategy '%s'", strategy->name());

  TSCont cont = TSContCreate(handle_hook, TSMutexCreate());

  auto strategyTxn           = new StrategyTxn();
  strategyTxn->strategy      = strategy;
  strategyTxn->txn           = strategy->newTxn();
  strategyTxn->request_count = 0;
  strategyTxn->prev_host     = nullptr;
  strategyTxn->prev_port     = 0;
  strategyTxn->prev_is_retry = false;
  TSContDataSet(cont, (void *)strategyTxn);

  // TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
  // TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_OS_DNS_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, cont);

  TSResponseAction ra;
  memset(&ra, 0, sizeof(TSResponseAction)); // because {0} gives a C++ warning. Ugh.
  constexpr const char *const exclude_host = nullptr;
  constexpr const size_t exclude_host_len  = 0;
  constexpr const in_port_t exclude_port   = 0;
  strategy->next(txnp, strategyTxn->txn, exclude_host, exclude_host_len, exclude_port, &ra.hostname, &ra.hostname_len, &ra.port,
                 &ra.is_retry);

  if (ra.hostname == nullptr) {
    // TODO make configurable
    TSDebug(PLUGIN_NAME, "TSRemapDoRemap strategy '%s' next returned nil, returning 502!", strategy->name());
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_BAD_GATEWAY);
    // TODO verify TS_EVENT_HTTP_TXN_CLOSE fires, and if not, free the cont here.
    return TSREMAP_DID_REMAP;
  }

  ra.fail          = false;
  ra.nextHopExists = true;
  ra.responseIsRetryable =
    true; // The action here is used for the very first connection, not any retry. So of course we should try it.
  ra.goDirect      = strategy->goDirect();
  ra.parentIsProxy = strategy->parentIsProxy();
  TSDebug(PLUGIN_NAME, "TSRemapDoRemap setting response_action hostname '%.*s' port %d direct %d proxy %d", int(ra.hostname_len),
          ra.hostname, ra.port, ra.goDirect, ra.parentIsProxy);
  TSHttpTxnResponseActionSet(txnp, &ra);

  return TSREMAP_NO_REMAP;
}

extern "C" tsapi void
TSRemapDeleteInstance(void *ih)
{
  TSDebug(PLUGIN_NAME, "TSRemapDeleteInstance calling");
  auto strategy = static_cast<TSNextHopSelectionStrategy *>(ih);
  delete strategy;
}
