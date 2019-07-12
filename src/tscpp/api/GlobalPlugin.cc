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
 * @file GlobalPlugin.cc
 */
#include "tscpp/api/GlobalPlugin.h"
#include "ts/ts.h"
#include <cstddef>
#include "tscpp/api/noncopyable.h"
#include "utils_internal.h"
#include "logging_internal.h"

using namespace atscppapi;

/**
 * @private
 */
struct atscppapi::GlobalPluginState : noncopyable {
  TSCont cont_ = nullptr;
  GlobalPlugin *global_plugin_;
  bool ignore_internal_;

  GlobalPluginState(GlobalPlugin *global_plugin, bool ignore_internal)
    : global_plugin_(global_plugin), ignore_internal_(ignore_internal)
  {
  }
};

int
GlobalPlugin::handleEvents(TSCont cont, TSEvent event, void *edata)
{
  GlobalPluginState *state = static_cast<GlobalPluginState *>(TSContDataGet(cont));

  LOG_DEBUG("GlobalPlugin::handleEvents() cont=%p, event=%d, edata=%p, plugin=%p", cont, event, edata, state->global_plugin_);

  std::lock_guard<Mutex> lock(*state->global_plugin_->getMutex()); // Not sure if this is needed.

  if (event == TS_EVENT_HTTP_SELECT_ALT) {
    TSHttpAltInfo altinfo_handle = static_cast<TSHttpAltInfo>(edata);

    TSMBuffer hdr_buf;
    TSMLoc hdr_loc;

    TSHttpAltInfoClientReqGet(altinfo_handle, &hdr_buf, &hdr_loc);
    const Request clientReq(hdr_buf, hdr_loc); // no MLocRelease needed

    TSHttpAltInfoCachedReqGet(altinfo_handle, &hdr_buf, &hdr_loc);
    const Request cachedReq(hdr_buf, hdr_loc); // no MLocRelease needed

    TSHttpAltInfoCachedRespGet(altinfo_handle, &hdr_buf, &hdr_loc);
    Response cachedResp;
    cachedResp.init(hdr_buf, hdr_loc); // no MLocRelease needed

    state->global_plugin_->handleSelectAlt(clientReq, cachedReq, cachedResp);

  } else if (event == TS_EVENT_HTTP_SSN_START) {
    TSHttpSsn ats_ssn_handle = static_cast<TSHttpSsn>(edata);
    if (state->ignore_internal_ && TSHttpSsnIsInternal(ats_ssn_handle)) {
      LOG_DEBUG("Ignoring event %d on internal transaction %p for global plugin %p", event, ats_ssn_handle, state->global_plugin_);
      TSHttpSsnReenable(ats_ssn_handle, TS_EVENT_HTTP_CONTINUE);
    } else {
      state->global_plugin_->handleSessionStart(*utils::internal::getSession(ats_ssn_handle));
    }
  } else {
    detail::invokeSessionPluginEventFunc(state->global_plugin_, event, edata, state->ignore_internal_);
  }

  return 0;
}

GlobalPlugin::GlobalPlugin(bool ignore_internal_)
{
  utils::internal::initManagement();
  state_        = new GlobalPluginState(this, ignore_internal_);
  TSMutex mutex = nullptr;
  state_->cont_ = TSContCreate(GlobalPlugin::handleEvents, mutex);
  TSContDataSet(state_->cont_, static_cast<void *>(state_));
}

GlobalPlugin::~GlobalPlugin()
{
  TSContDestroy(state_->cont_);
  delete state_;
}

void
GlobalPlugin::registerHook(GlobalPluginHooks::HookType hook_type)
{
  TSHttpHookID hook_id = utils::internal::convertInternalHookToTsHook(hook_type);
  TSHttpHookAdd(hook_id, state_->cont_);
  LOG_DEBUG("Registered global plugin %p for hook %s", this, HOOK_TYPE_STRINGS[hook_type].c_str());
}

void
GlobalPlugin::registerHook(SessionPluginHooks::HookType hook_type)
{
  TSHttpHookID hook_id = utils::internal::convertInternalHookToTsHook(hook_type);
  TSHttpHookAdd(hook_id, state_->cont_);
  LOG_DEBUG("Registered global plugin %p for hook %s", this, SessionPluginHooks::HOOK_TYPE_STRINGS[hook_type].c_str());
}

void
GlobalPlugin::registerHook(TransactionPluginHooks::HookType hook_type)
{
  TSHttpHookID hook_id = utils::internal::convertInternalHookToTsHook(hook_type);
  TSHttpHookAdd(hook_id, state_->cont_);
  LOG_DEBUG("Registered global plugin %p for hook %s", this, TransactionPluginHooks::HOOK_TYPE_STRINGS[hook_type].c_str());
}

bool
atscppapi::RegisterGlobalPlugin(const char *name, const char *vendor, const char *email)
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = name;
  info.vendor_name   = vendor;
  info.support_email = email;

  bool success = (TSPluginRegister(&info) == TS_SUCCESS);
  if (!success) {
    TSError("[Plugin.cc] Plugin registration failed");
  }
  return success;
}
