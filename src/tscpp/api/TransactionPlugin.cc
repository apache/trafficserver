/**
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

/**
 * @file TransactionPlugin.cc
 */

#include <cstddef>
#include <cassert>
#include "ts/ts.h"
#include "tscpp/api/TransactionPlugin.h"
#include "utils_internal.h"
#include "tscpp/api/noncopyable.h"
#include "logging_internal.h"

using namespace atscppapi;
using atscppapi::TransactionPlugin;

/**
 * @private
 */
struct atscppapi::TransactionPluginState : noncopyable {
  TSCont cont_ = nullptr;
  TSHttpTxn ats_txn_handle_;
  TransactionPluginState(TSHttpTxn ats_txn_handle) : ats_txn_handle_(ats_txn_handle) {}
};

namespace
{
static int
handleTransactionPluginEvents(TSCont cont, TSEvent event, void *edata)
{
  TransactionPlugin *plugin = static_cast<TransactionPlugin *>(TSContDataGet(cont));
  LOG_DEBUG("cont=%p, event=%d, tshttptxn=%p, plugin=%p", cont, event, edata, plugin);
  std::lock_guard<Mutex> lock(*utils::internal::getTransactionPluginMutex(*plugin)); // Not sure if this is needed.
  atscppapi::detail::invokeTransactionPluginEventFunc(plugin, event, edata);
  return 0;
}

} /* anonymous namespace */

TransactionPlugin::TransactionPlugin(Transaction &transaction)
{
  state_        = new TransactionPluginState(static_cast<TSHttpTxn>(transaction.getAtsHandle()));
  TSMutex mutex = nullptr;
  state_->cont_ = TSContCreate(handleTransactionPluginEvents, mutex);
  TSContDataSet(state_->cont_, static_cast<void *>(this));
  LOG_DEBUG("Creating new TransactionPlugin=%p tshttptxn=%p, cont=%p", this, state_->ats_txn_handle_, state_->cont_);
}

bool
TransactionPlugin::isWebsocket() const
{
  return TSHttpTxnIsWebsocket(state_->ats_txn_handle_);
}

bool
TransactionPlugin::transactionObjExists()
{
  return utils::internal::getTransaction(state_->ats_txn_handle_, false) != nullptr;
}

Transaction &
TransactionPlugin::getTransaction()
{
  Transaction *p = utils::internal::getTransaction(state_->ats_txn_handle_, false);
  assert(p);
  return *p;
}

TransactionPlugin::~TransactionPlugin()
{
  LOG_DEBUG("Destroying TransactionPlugin=%p", this);
  TSContDestroy(state_->cont_);
  delete state_;
}

void
TransactionPlugin::registerHook(TransactionPluginHooks::HookType hook_type)
{
  LOG_DEBUG("TransactionPlugin=%p tshttptxn=%p registering hook_type=%d [%s]", this, state_->ats_txn_handle_, hook_type,
            HOOK_TYPE_STRINGS[hook_type].c_str());
  TSHttpHookID hook_id = atscppapi::utils::internal::convertInternalHookToTsHook(hook_type);
  TSHttpTxnHookAdd(state_->ats_txn_handle_, hook_id, state_->cont_);
}

namespace atscppapi
{
namespace detail
{
  void
  invokeTransactionPluginEventFunc(TransactionPluginHooks *plugin, TSEvent event, void *edata, bool ignore_internal)
  {
    void (TransactionPluginHooks::*mfp)(Transaction &);
    switch (event) {
    case TS_EVENT_HTTP_PRE_REMAP:
      mfp = &TransactionPluginHooks::handleReadRequestHeadersPreRemap;
      break;
    case TS_EVENT_HTTP_POST_REMAP:
      mfp = &TransactionPluginHooks::handleReadRequestHeadersPostRemap;
      break;
    case TS_EVENT_HTTP_SEND_REQUEST_HDR:
      mfp = &TransactionPluginHooks::handleSendRequestHeaders;
      break;
    case TS_EVENT_HTTP_READ_RESPONSE_HDR:
      mfp = &TransactionPluginHooks::handleReadResponseHeaders;
      break;
    case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
      mfp = &TransactionPluginHooks::handleSendResponseHeaders;
      break;
    case TS_EVENT_HTTP_OS_DNS:
      mfp = &TransactionPluginHooks::handleOsDns;
      break;
    case TS_EVENT_HTTP_READ_REQUEST_HDR:
      mfp = &TransactionPluginHooks::handleReadRequestHeaders;
      break;
    case TS_EVENT_HTTP_READ_CACHE_HDR:
      mfp = &TransactionPluginHooks::handleReadCacheHeaders;
      break;
    case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
      mfp = &TransactionPluginHooks::handleReadCacheLookupComplete;
      break;
    default:
      assert(false); /* we should never get here */
      break;
    }

    TSHttpTxn ats_txn_handle = static_cast<TSHttpTxn>(edata);
    if (ignore_internal && TSHttpTxnIsInternal(ats_txn_handle)) {
      LOG_DEBUG("Ignoring event %d on internal transaction %p for global plugin %p", event, ats_txn_handle, plugin);
      TSHttpTxnReenable(ats_txn_handle, TS_EVENT_HTTP_CONTINUE);
    } else {
      (plugin->*mfp)(*utils::internal::getTransaction(ats_txn_handle));
    }
  }

} // end namespace detail
} // end namespace atscppapi
