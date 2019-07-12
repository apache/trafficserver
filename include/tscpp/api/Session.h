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
 * @file Session.h
 */

#pragma once

#include <memory>
#include <sys/socket.h>
#include <list>
#include "tscpp/api/noncopyable.h"
#include <ts/apidefs.h>
namespace atscppapi
{
// forward declarations
class SessionPlugin;
struct SessionState;
namespace utils
{
  class internal;
}

/**
 * @brief Sessions are the object containing all the state related to a HTTP Session
 *
 * @warning Sessions should never be directly created by the user, they will always be automatically
 * created and destroyed as they are needed. Sessions should never be saved beyond the
 * scope of the function in which they are delivered otherwise undefined behaviour will result.
 */
class Session : noncopyable
{
public:
  /**
   * @brief ContextValues are a mechanism to share data between plugins using the atscppapi.
   *
   * Any data can be shared so long as it extends ContextValue, a simple example might
   * be:
   *
   * \code
   *     struct mydata : ContextValue {
   *       int id_;
   *       string foo_;
   *       mydata(int id, string foo) : id_(id), foo_(foo) { }
   *     }
   *
   *     Session.setContextValue("some-key", std::shared_ptr(new mydata(12, "hello")));
   *
   *     // From another plugin you'll have access to this contextual data:
   *     std::shared_ptr<Session.getContextValue("some-key")
   *
   * \endcode
   *
   * Because getContextValue() and setContextValue()
   * take shared pointers you dont have to worry about the cleanup as that will happen automatically so long
   * as you dont have std::shared_ptrs that cannot go out of scope.
   */
  class ContextValue
  {
  public:
    virtual ~ContextValue() {}
  };

  ~Session();

  /**
   * Context Values are a way to share data between plugins, the key is always a string
   * and the value can be a std::shared_ptr to any type that extends ContextValue.
   * @param key the key to search for.
   * @return Shared pointer that is correctly initialized if the
   *         value existed. It should be checked with .get() != nullptr before use.
   */
  std::shared_ptr<ContextValue> getContextValue(const std::string &key);

  /**
   * Context Values are a way to share data between plugins, the key is always a string
   * and the value can be a std::shared_ptr to any type that extends ContextValue.
   * @param key the key to insert.
   * @param value a shared pointer to a class that extends ContextValue.
   */
  void setContextValue(const std::string &key, std::shared_ptr<ContextValue> value);

  /**
   * Causes the Session to continue on to other states in the HTTP state machine
   * If you do not call resume() on a Session it will remain in that state until
   * it's advanced out by a call to resume() or error().
   */
  void resume();

  /**
   * Causes the Session to advance to the error state in the HTTP state machine.
   * @see error(const std::string &)
   */
  void error();

  /**
   * Get the clients address
   * @return The sockaddr structure representing the client's address
   * @see atscppapi::utils::getIpString() in atscppapi/utils.h
   * @see atscppapi::utils::getPort() in atscppapi/utils.h
   * @see atscppapi::utils::getIpPortString in atscppapi/utils.h
   */
  const sockaddr *getClientAddress() const;

  /**
   * Get the ATS-side socket address for the ATS-Client connection.
   * @return The sockaddr structure representing the client's address
   */
  const sockaddr *getIncomingAddress() const;

  /**
   * Returns the TSHttpSsn related to the current Session
   *
   * @return a void * which can be cast back to a TSHttpSsn
   */
  void *getAtsHandle() const;

  /**
   * Adds a SessionPlugin to the current Session. This effectively transfers ownership and the
   * Session is now responsible for cleaning it up.
   *
   * @param SessionPlugin* the SessionPlugin that will be now bound to the current Session.
   */
  void addPlugin(SessionPlugin *);

  bool isInternalRequest() const;

private:
  SessionState *state_; //!< The internal SessionState object tied to the current Session

  /**
   * @private
   *
   * @param a void pointer that represents a TSHttpSsn
   */
  Session(void *);

  /**
   * Set the @a event for the currently active hook.
   */
  void setEvent(TSEvent event);

  /**
   * Reset all the transaction handles (for response/requests).
   * This is used to clear handles that may have gone stale.
   *
   * @private
   */

  /**
   * Returns a list of SessionPlugin pointers bound to the current Session
   *
   * @private
   *
   * @return a std::list<SessionPlugin *> which represents all SessionPlugin bound to the current Session.
   */
  const std::list<SessionPlugin *> &getPlugins() const;

  friend class utils::internal;
};

} // namespace atscppapi
