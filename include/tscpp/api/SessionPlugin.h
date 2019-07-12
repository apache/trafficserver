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
 * @file SessionPlugin.h
 * @brief Contains the interface used in creating Session plugins.
 */

#pragma once

#include <memory>

#include "tscpp/api/SessionPluginHooks.h"
#include "tscpp/api/Session.h"

namespace atscppapi
{
namespace utils
{
  class internal;
} // namespace utils

/**
 * @private
 */
struct SessionPluginState;

/**
 * @brief The interface used when creating a SessionPlugin.
 *
 * A Session Plugin is a plugin that will fire only for the specific Session it
 * was bound to. When you create a SessionPlugin you call the parent constructor with
 * the associated Session and it will become automatically bound. This means that your
 * SessionPlugin will automatically be destroyed when the Session dies.
 *
 * Implications of this are that you can easily add Session scoped storage by adding
 * a member to a SessionPlugin since the destructor will be called of your SessionPlugin
 * any cleanup that needs to happen can happen in your destructor as you normally would.
 *
 * You must always be sure to implement the appropriate callback for the type of hook you register.
 *
 * \code
 * class SessionHookPlugin : public SessionPlugin {
 * public:
 *   SessionHookPlugin(Session &session) : SessionPlugin(session) {
 *     char_ptr_ = new char[100]; // Session scoped storage
 *     registerHook(HOOK_SEND_RESPONSE_HEADERS);
 *   }
 *   virtual ~SessionHookPlugin() {
 *     delete[] char_ptr_; // cleanup
 *   }
 *   void handleSendResponseHeaders(Session &session) {
 *    session.resume();
 *   }
 * private:
 *   char *char_ptr_;
 * };
 * \endcode
 *
 */
class SessionPlugin : public SessionPluginHooks
{
public:
  /**
   * registerHook is the mechanism used to attach a session hook.
   *
   * \note Whenever you register a hook you must have the appropriate callback defined in your SessionPlugin
   *  see Session::HookType and SessionPluginHooks :w for the corresponding hook typesand callback methods.
   *  If you fail to implement the callback, a default implementation will be used that will only resume the Session.
   *
   * \note Put actions on Session close in the derived class destructor.
   *
   * @param HookType the type of hook you wish to register
   */
  void registerHook(HookType hook_type);
  void registerHook(TransactionPluginHooks::HookType hook_type);
  ~SessionPlugin() override;

protected:
  SessionPlugin(Session &);

  // Returns true if an instance of the Session class exists for the session associated with this
  // SessionPlugin instance.  (A Session instance will exist if a plugin hook has been executed
  // where the handler function takes a reference to Session as its parameter).
  //
  bool sessionObjExists();

  // Returns a reference to the instance of the Session class exists for the session associated
  // with this SessionPlugin instance.  Calling this function will cause ATS to abort if
  // sessionObjExits() returns false.
  //
  Session &getSession();

private:
  SessionPluginState *state_; /**< The internal state for a SessionPlugin */
  friend class utils::internal;
};

} // end namespace atscppapi
