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
 * @file GlobalPlugin.h
 * @brief Contains the interface used in creating Global plugins.
 */

#pragma once

#include "tscpp/api/Plugin.h"

namespace atscppapi
{
struct GlobalPluginState;

/**
 * @brief The interface used when creating a GlobalPlugin.
 *
 * A GlobalPlugin is a Plugin that will fire for a given hook on all Transactions.
 * In other words, a GlobalPlugin is not tied to a specific plugin, a Transaction
 * specific plugin would be a TransactionPlugin.
 *
 * Depending on the
 * type of hook you choose to build you will implement one or more callback methods.
 * Here is a simple example of a GlobalPlugin:
 *
 * \code
 * class GlobalHookPlugin : public GlobalPlugin {
 * public:
 *  GlobalHookPlugin() {
 *   registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
 *  }
 *  virtual void handleReadRequestHeadersPreRemap(Transaction &transaction) {
 *    std::cout << "Hello from handleReadRequesHeadersPreRemap!" << std::endl;
 *    transaction.resume();
 *  }
 * };
 * \endcode
 * @see Plugin
 */
class GlobalPlugin : public Plugin
{
public:
  /**
   * registerHook is the mechanism used to attach a global hook.
   *
   * \note Whenever you register a hook you must have the appropriate callback defined in your GlobalPlugin
   *  see HookType and Plugin for the correspond HookTypes and callback methods. If you fail to implement the
   *  callback, a default implementation will be used that will only resume the Transaction.
   *
   * @param HookType the type of hook you wish to register
   * @see HookType
   * @see Plugin
   */
  void registerHook(Plugin::HookType);
  ~GlobalPlugin() override;

protected:
  /**
   * Constructor.
   *
   * @param ignore_internal_transactions When true, all hooks registered by this plugin are ignored
   *                                     for internal transactions (internal transactions are created
   *                                     when other plugins create requests). Defaults to false.
   */
  GlobalPlugin(bool ignore_internal_transactions = false);

private:
  GlobalPluginState *state_; /**< Internal state tied to a GlobalPlugin */
};

} // namespace atscppapi
