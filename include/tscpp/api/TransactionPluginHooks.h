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
 * @file TransactionPluginHooks.h
 *
 * @brief Contains the base interface used in creating Global, Session  and Transaction plugins.
 * \note This interface can never be implemented directly, it should be implemented
 *   through extending GlobalPlugin, SessionPlugin, TransactionPlugin, or TransformationPlugin.
 */

#pragma once

#include <memory>
#include <mutex>

#include "tscpp/api/Request.h"
#include "tscpp/api/Transaction.h"
#include "tscpp/api/noncopyable.h"

namespace atscppapi
{
#if !defined(ATSCPPAPI_MUTEX_DEFINED_)
#define ATSCPPAPI_MUTEX_DEFINED_

using Mutex = std::recursive_mutex;

#endif

/**
 * @brief The base interface used when creating a Plugin.
 *
 * \note This interface can never be implemented directly, it should be implemented
 *   through extending GlobalPlugin, SessionPlugin, TransactionPlugin, or TransformationPlugin.
 *
 * @see TransactionPlugin
 * @see SessionPlugin
 * @see GlobalPlugin
 * @see TransformationPlugin
 */
class TransactionPluginHooks : noncopyable
{
public:
  /**
   * A enumeration of the available types of Hooks. These are used with GlobalPlugin::registerHook()
   * SessionPlugin::registerHook(), and TransactionPlugin::registerHook().
   */
  enum HookType {
    HOOK_READ_REQUEST_HEADERS_PRE_REMAP = 0, /**< This hook will be fired before remap has occurred. */
    HOOK_READ_REQUEST_HEADERS_POST_REMAP,    /**< This hook will be fired directly after remap has occurred. */
    HOOK_SEND_REQUEST_HEADERS,               /**< This hook will be fired right before request headers are sent to the origin */
    HOOK_READ_RESPONSE_HEADERS, /**< This hook will be fired right after response headers have been read from the origin */
    HOOK_SEND_RESPONSE_HEADERS, /**< This hook will be fired right before the response headers are sent to the client */
    HOOK_OS_DNS,                /**< This hook will be fired right after the OS DNS lookup */
    HOOK_READ_REQUEST_HEADERS,  /**< This hook will be fired after the request is read. */
    HOOK_READ_CACHE_HEADERS,    /**< This hook will be fired after the CACHE hdrs. */
    HOOK_CACHE_LOOKUP_COMPLETE, /**< This hook will be fired after cache lookup complete. */
  };

  /**< Human readable strings for each HookType, you can access them as HOOK_TYPE_STRINGS[HOOK_OS_DNS] for example. */
  static std::string const HOOK_TYPE_STRINGS[];

  /**
   * This method must be implemented when you hook HOOK_READ_REQUEST_HEADERS_PRE_REMAP
   */
  virtual void
  handleReadRequestHeadersPreRemap(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_READ_REQUEST_HEADERS_POST_REMAP
   */
  virtual void
  handleReadRequestHeadersPostRemap(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_SEND_REQUEST_HEADERS
   */
  virtual void
  handleSendRequestHeaders(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_READ_RESPONSE_HEADERS
   */
  virtual void
  handleReadResponseHeaders(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_SEND_RESPONSE_HEADERS
   */
  virtual void
  handleSendResponseHeaders(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_OS_DNS
   */
  virtual void
  handleOsDns(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_READ_REQUEST_HEADERS
   */
  virtual void
  handleReadRequestHeaders(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_READ_CACHE_HEADERS
   */
  virtual void
  handleReadCacheHeaders(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_CACHE_LOOKUP_COMPLETE
   */
  virtual void
  handleReadCacheLookupComplete(Transaction &transaction)
  {
    transaction.resume();
  };

  /**
   * This method must be implemented when you hook HOOK_SELECT_ALT
   */
  virtual void handleSelectAlt(const Request &clientReq, const Request &cachedReq, const Response &cachedResp){};

  virtual ~TransactionPluginHooks(){};

protected:
  /**
   * \note This interface can never be implemented directly, it should be implemented
   *   through extending GlobalPlugin, SessionPlugin, TransactionPlugin, or TransformationPlugin.
   *
   * @private
   */
  TransactionPluginHooks(){};

  /**
   * This method will return a std::shared_ptr to a Mutex that can be used for AsyncProvider and AsyncReceiver operations.
   *
   * If another thread wanted to stop this transaction from dispatching an event it could be passed
   * this mutex and it would be able to lock it and prevent another thread from dispatching back into this
   * TransactionPlugin.
   */
  std::shared_ptr<Mutex>
  getMutex()
  {
    return mutex_;
  }

private:
  std::shared_ptr<Mutex> mutex_{new Mutex};
};

} // namespace atscppapi
