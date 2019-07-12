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
 * @file TransactionPlugin.h
 * @brief Contains the interface used in creating Transaction plugins.
 */

#pragma once

#include <memory>

#include "tscpp/api/TransactionPluginHooks.h"
#include "tscpp/api/Transaction.h"

namespace atscppapi
{
namespace utils
{
  class internal;
} // namespace utils

/**
 * @private
 */
struct TransactionPluginState;

/**
 * @brief The interface used when creating a TransactionPlugin.
 *
 * A Transaction Plugin is a plugin that will fire only for the specific Transaction it
 * was bound to. When you create a TransactionPlugin you call the parent constructor with
 * the associated Transaction and it will become automatically bound. This means that your
 * TransactionPlugin will automatically be destroyed when the Transaction dies.
 *
 * Implications of this are that you can easily add Transaction scoped storage by adding
 * a member to a TransactionPlugin since the destructor will be called of your TransactionPlugin
 * any cleanup that needs to happen can happen in your destructor as you normally would.
 *
 * You must always be sure to implement the appropriate callback for the type of hook you register.
 *
 * \code
 * // For a more detailed example see example/cppapi/transactionhook/
 * class TransactionHookPlugin : publicTransactionPlugin {
 * public:
 *   TransactionHookPlugin(Transaction &transaction) : TransactionPlugin(transaction) {
 *     char_ptr_ = new char[100]; // Transaction scoped storage
 *     registerHook(HOOK_SEND_RESPONSE_HEADERS);
 *   }
 *   virtual ~TransactionHookPlugin() {
 *     delete[] char_ptr_; // cleanup
 *   }
 *   void handleSendResponseHeaders(Transaction &transaction) {
 *     transaction.resume();
 *   }
 * private:
 *   char *char_ptr_;
 * };
 * \endcode
 *
 * @see Plugin
 * @see HookType
 */
class TransactionPlugin : public TransactionPluginHooks
{
public:
  /**
   * registerHook is the mechanism used to attach a transaction hook.
   *
   * \note Whenever you register a hook you must have the appropriate callback defined in your TransactionPlugin
   *  see HookType and TransactionPluginHooks for the correspond HookTypes and callback methods. If you fail to
   *  implement the callback, a default implementation will be used that will only resume the Transaction.
   *
   * \note Put actions on Transaction close in the derived class destructor.
   *
   * @param HookType the type of hook you wish to register
   * @see HookType
   * @see TransactionPluginHooks
   */
  void registerHook(HookType hook_type);
  ~TransactionPlugin() override;

  bool isWebsocket() const;

protected:
  TransactionPlugin(Transaction &transaction);

  // Returns true if an instance of the Transaction class exists for the transaction associated with this
  // TransactionPlugin instance.  (A Transaction instance will exist if a plugin hook has been executed
  // where the handler function takes a reference to Transaction as its parameter).
  //
  bool transactionObjExists();

  // Returns a reference to the instance of the Transaction class exists for the transaction associated
  // with this TransactionPlugin instance.  Calling this function will cause ATS to abort if
  // transactionObjExits() returns false.
  //
  Transaction &getTransaction();

private:
  TransactionPluginState *state_; /**< The internal state for a TransactionPlugin */
  friend class utils::internal;
};

} // end namespace atscppapi
