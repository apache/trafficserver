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

#include "atscppapi/TransactionPlugin.h"
#include <ts/ts.h>
#include <cstddef>
#include <cassert>
#include "atscppapi/Mutex.h"
#include "atscppapi/shared_ptr.h"
#include "utils_internal.h"
#include "atscppapi/noncopyable.h"
#include "logging_internal.h"

using namespace atscppapi;
using atscppapi::TransactionPlugin;

/**
 * @private
 */
struct atscppapi::TransactionPluginState : noncopyable {
  TSCont cont_;
  TSHttpTxn ats_txn_handle_;
  shared_ptr<Mutex> mutex_;
  TransactionPluginState(TSHttpTxn ats_txn_handle) : ats_txn_handle_(ats_txn_handle), mutex_(new Mutex(Mutex::TYPE_RECURSIVE)) {}
};

namespace
{
static int
handleTransactionPluginEvents(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn txn             = static_cast<TSHttpTxn>(edata);
  TransactionPlugin *plugin = static_cast<TransactionPlugin *>(TSContDataGet(cont));
  LOG_DEBUG("cont=%p, event=%d, tshttptxn=%p, plugin=%p", cont, event, edata, plugin);
  atscppapi::utils::internal::invokePluginForEvent(plugin, txn, event);
  return 0;
}

} /* anonymous namespace */

TransactionPlugin::TransactionPlugin(Transaction &transaction)
{
  state_        = new TransactionPluginState(static_cast<TSHttpTxn>(transaction.getAtsHandle()));
  TSMutex mutex = NULL;
  state_->cont_ = TSContCreate(handleTransactionPluginEvents, mutex);
  TSContDataSet(state_->cont_, static_cast<void *>(this));
  LOG_DEBUG("Creating new TransactionPlugin=%p tshttptxn=%p, cont=%p", this, state_->ats_txn_handle_, state_->cont_);
}

shared_ptr<Mutex>
TransactionPlugin::getMutex()
{
  return state_->mutex_;
}

TransactionPlugin::~TransactionPlugin()
{
  LOG_DEBUG("Destroying TransactionPlugin=%p", this);
  TSContDestroy(state_->cont_);
  delete state_;
}

void
TransactionPlugin::registerHook(Plugin::HookType hook_type)
{
  LOG_DEBUG("TransactionPlugin=%p tshttptxn=%p registering hook_type=%d [%s]", this, state_->ats_txn_handle_, hook_type,
            HOOK_TYPE_STRINGS[hook_type].c_str());
  TSHttpHookID hook_id = atscppapi::utils::internal::convertInternalHookToTsHook(hook_type);
  TSHttpTxnHookAdd(state_->ats_txn_handle_, hook_id, state_->cont_);
}
