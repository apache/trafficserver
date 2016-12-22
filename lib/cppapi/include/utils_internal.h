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
#ifndef ATSCPPAPI_ATSUTILS_H_
#define ATSCPPAPI_ATSUTILS_H_

#include <ts/ts.h>
#include <string>
#include "atscppapi/GlobalPlugin.h"
#include "atscppapi/TransactionPlugin.h"
#include "atscppapi/TransformationPlugin.h"
#include "atscppapi/Plugin.h"
#include "atscppapi/HttpVersion.h"
#include "atscppapi/utils.h"
#include "atscppapi/AsyncHttpFetch.h"
#include "atscppapi/Transaction.h"
#include "atscppapi/InterceptPlugin.h"

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

} /* utils */
}

#endif /* ATSCPPAPI_ATSUTILS_H_ */
