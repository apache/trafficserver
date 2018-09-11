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
 * @file atsutils.h
 *
 *
 * @brief internal utilities for atscppapi
 */

#pragma once

#include "ts/ts.h"
#include <string>
#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/TransactionPlugin.h"
#include "tscpp/api/TransformationPlugin.h"
#include "tscpp/api/Plugin.h"
#include "tscpp/api/HttpVersion.h"
#include "tscpp/api/utils.h"
#include "tscpp/api/AsyncHttpFetch.h"
#include "tscpp/api/Transaction.h"
#include "tscpp/api/InterceptPlugin.h"

namespace atscppapi
{
namespace utils
{
  /**
   * @private
   */
  class internal
  {
  public:
    static TSHttpHookID convertInternalHookToTsHook(Plugin::HookType);
    static TSHttpHookID convertInternalTransformationTypeToTsHook(TransformationPlugin::Type type);
    static void invokePluginForEvent(TransactionPlugin *, TSHttpTxn, TSEvent);
    static void invokePluginForEvent(GlobalPlugin *, TSHttpTxn, TSEvent);
    static void invokePluginForEvent(GlobalPlugin *, TSHttpAltInfo, TSEvent);
    static HttpVersion getHttpVersion(TSMBuffer hdr_buf, TSMLoc hdr_loc);
    static void initTransactionManagement();
    static std::string consumeFromTSIOBufferReader(TSIOBufferReader);
    static std::shared_ptr<Mutex> getTransactionPluginMutex(TransactionPlugin &);
    static Transaction &getTransaction(TSHttpTxn);

    static AsyncHttpFetchState *
    getAsyncHttpFetchState(AsyncHttpFetch &async_http_fetch)
    {
      return async_http_fetch.state_;
    }

    static void
    setTransactionEvent(Transaction &transaction, TSEvent event)
    {
      transaction.setEvent(event);
    }

    static void
    resetTransactionHandles(Transaction &transaction)
    {
      transaction.resetHandles();
    }

    static void
    initResponse(Response &response, TSMBuffer hdr_buf, TSMLoc hdr_loc)
    {
      response.init(hdr_buf, hdr_loc);
    }

    static const std::list<TransactionPlugin *> &
    getTransactionPlugins(const Transaction &transaction)
    {
      return transaction.getPlugins();
    }

    static void
    dispatchInterceptEvent(InterceptPlugin *plugin, TSEvent event, void *edata)
    {
      plugin->handleEvent(static_cast<int>(event), edata);
    }

    static void
    deleteAsyncHttpFetch(AsyncHttpFetch *fetch)
    {
      delete fetch;
    }

  }; /* internal */

} // namespace utils
} // namespace atscppapi
