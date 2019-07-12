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
 * @file SessionPlugin.cc
 */

#include <cassert>

#include "ts/ts.h"
#include "tscpp/api/SessionPlugin.h"

#include "utils_internal.h"
#include "logging_internal.h"

using namespace atscppapi;
using atscppapi::SessionPlugin;

/**
 * @private
 */
struct atscppapi::SessionPluginState : noncopyable {
  TSCont cont_ = nullptr;
  TSHttpSsn ats_ssn_handle_;
  SessionPluginState(TSHttpSsn ats_ssn_handle) : ats_ssn_handle_(ats_ssn_handle) {}
};

namespace
{
static int
handleSessionPluginEvents(TSCont cont, TSEvent event, void *edata)
{
  SessionPlugin *plugin = static_cast<SessionPlugin *>(TSContDataGet(cont));
  LOG_DEBUG("cont=%p, event=%d, tshttpssn=%p, plugin=%p", cont, event, edata, plugin);
  std::lock_guard<Mutex> lock(*utils::internal::getSessionPluginMutex(*plugin)); // Not sure if this is needed.
  atscppapi::detail::invokeSessionPluginEventFunc(plugin, event, edata);
  return 0;
}

} /* anonymous namespace */

SessionPlugin::SessionPlugin(Session &session)
{
  state_        = new SessionPluginState(static_cast<TSHttpSsn>(session.getAtsHandle()));
  TSMutex mutex = nullptr;
  state_->cont_ = TSContCreate(handleSessionPluginEvents, mutex);
  TSContDataSet(state_->cont_, static_cast<void *>(this));
  LOG_DEBUG("Creating new SessionPlugin=%p tshttptxn=%p, cont=%p", this, state_->ats_ssn_handle_, state_->cont_);
}

SessionPlugin::~SessionPlugin()
{
  LOG_DEBUG("Destroying SessionPlugin=%p", this);
  TSContDestroy(state_->cont_);
  delete state_;
}

void
SessionPlugin::registerHook(SessionPluginHooks::HookType hook_type)
{
  LOG_DEBUG("SessionPlugin=%p tshttptxn=%p registering hook_type=%d [%s]", this, state_->ats_ssn_handle_, hook_type,
            HOOK_TYPE_STRINGS[hook_type].c_str());
  TSHttpHookID hook_id = atscppapi::utils::internal::convertInternalHookToTsHook(hook_type);
  TSHttpSsnHookAdd(state_->ats_ssn_handle_, hook_id, state_->cont_);
}

void
SessionPlugin::registerHook(TransactionPluginHooks::HookType hook_type)
{
  TSHttpHookID hook_id = utils::internal::convertInternalHookToTsHook(hook_type);
  TSHttpSsnHookAdd(state_->ats_ssn_handle_, hook_id, state_->cont_);
  LOG_DEBUG("Registered global plugin %p for hook %s", this, TransactionPluginHooks::HOOK_TYPE_STRINGS[hook_type].c_str());
}

bool
SessionPlugin::sessionObjExists()
{
  return utils::internal::getSession(state_->ats_ssn_handle_, false) != nullptr;
}

Session &
SessionPlugin::getSession()
{
  Session *p = utils::internal::getSession(state_->ats_ssn_handle_);
  assert(p);
  return *p;
}

namespace atscppapi
{
namespace detail
{
  void
  invokeSessionPluginEventFunc(SessionPluginHooks *plugin, TSEvent event, void *edata, bool ignore_internal)
  {
    switch (event) {
    case TS_EVENT_HTTP_TXN_START: {
      TSHttpTxn ats_txn_handle = static_cast<TSHttpTxn>(edata);
      if (ignore_internal && TSHttpTxnIsInternal(ats_txn_handle)) {
        LOG_DEBUG("Ignoring event %d on internal transaction %p for session plugin %p", event, ats_txn_handle, plugin);
        TSHttpTxnReenable(ats_txn_handle, TS_EVENT_HTTP_CONTINUE);
      } else {
        plugin->handleTransactionStart(*utils::internal::getTransaction(ats_txn_handle));
      }
      break;
    }
    default: {
      invokeTransactionPluginEventFunc(plugin, event, edata, ignore_internal);
      break;
    }
    }
  }

} // end namespace detail
} // end namespace atscppapi
