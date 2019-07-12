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

#include "tscpp/api/GlobalPluginHooks.h"

namespace atscppapi
{
struct GlobalPluginState;

/**
 * @brief The interface used when creating a GlobalPlugin.
 *
 * A GlobalPlugin is a Plugin that will fire for a given hook on all Sessions (for session
 * hooks) or all Transactions (for transaction hooks).
 * In other words, a GlobalPlugin is not tied to a specific plugin, a Transaction
 * specific plugin would be a TransactionPlugin.
 *
 * Depending on the type of hook you choose to build you will implement one or more callback methods.
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
 */
class GlobalPlugin : public GlobalPluginHooks
{
public:
  /**
   * registerHook is the mechanism used to attach a global hook.
   *
   * \note Whenever you register a hook you must have the appropriate callback defined in your GlobalPlugin.
   *  If you fail to implement the callback, a default implementation will be used that will only resume the
   *  Session / Transaction.
   *
   * @param HookType the type of hook you wish to register
   * @see HookType
   * @see TransactionPluginHooks
   * @see SessionPluginHooks
   */
  void registerHook(TransactionPluginHooks::HookType);
  void registerHook(SessionPluginHooks::HookType);
  void registerHook(HookType);

  ~GlobalPlugin() override;

protected:
  /**
   * Constructor.
   *
   * @param ignore_internal When true, all hooks registered by this plugin are ignored for internal requests.
   *        Defaults to false.
   */
  GlobalPlugin(bool ignore_internal = false);

private:
  GlobalPluginState *state_; /**< Internal state tied to a GlobalPlugin */

  static int handleEvents(TSCont cont, TSEvent event, void *edata);
};

bool RegisterGlobalPlugin(const char *name, const char *vendor, const char *email);
inline bool
RegisterGlobalPlugin(std::string const &name, std::string const &vendor, std::string const &email)
{
  return RegisterGlobalPlugin(name.c_str(), vendor.c_str(), email.c_str());
}

} // namespace atscppapi
