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
 * @file Session.cc
 */

#include <unordered_map>
#include <string>

#include "tscpp/api/Session.h"
#include "logging_internal.h"
#include "utils_internal.h"
#include "tscpp/api/noncopyable.h"

using std::unordered_map;
using std::string;
using namespace atscppapi;

/**
 * @private
 */
struct atscppapi::SessionState : noncopyable {
  TSHttpSsn ssn_;
  TSEvent event_; ///< Current event being dispatched.
  std::list<SessionPlugin *> plugins_;
  unordered_map<string, std::shared_ptr<Session::ContextValue>> context_values_;

  SessionState(TSHttpSsn ssn) : ssn_(ssn), event_(TS_EVENT_NONE){};
};

Session::Session(void *raw_ssn)
{
  TSHttpSsn ssn = static_cast<TSHttpSsn>(raw_ssn);

  state_ = new SessionState(ssn);
  LOG_DEBUG("Session tshttpssn=%p constructing Session object %p", ssn, this);
}

Session::~Session()
{
  LOG_DEBUG("Session tshttpssn=%p destroying Session object %p", state_->ssn_, this);
  delete state_;
}

void
Session::setEvent(TSEvent event)
{
  state_->event_ = event;
}

void
Session::resume()
{
  TSHttpSsnReenable(state_->ssn_, static_cast<TSEvent>(TS_EVENT_HTTP_CONTINUE));
}

void
Session::error()
{
  LOG_DEBUG("Session tshttpssn=%p reenabling to error state", state_->ssn_);
  TSHttpSsnReenable(state_->ssn_, static_cast<TSEvent>(TS_EVENT_HTTP_ERROR));
}

bool
Session::isInternalRequest() const
{
  return (0 != TSHttpSsnIsInternal(state_->ssn_));
}

void *
Session::getAtsHandle() const
{
  return static_cast<void *>(state_->ssn_);
}

const std::list<atscppapi::SessionPlugin *> &
Session::getPlugins() const
{
  return state_->plugins_;
}

void
Session::addPlugin(SessionPlugin *plugin)
{
  LOG_DEBUG("Session tshttpssn=%p registering new SessionPlugin %p.", state_->ssn_, plugin);
  state_->plugins_.push_back(plugin);
}

std::shared_ptr<Session::ContextValue>
Session::getContextValue(const std::string &key)
{
  std::shared_ptr<Session::ContextValue> return_context_value;
  unordered_map<string, std::shared_ptr<Session::ContextValue>>::iterator iter = state_->context_values_.find(key);
  if (iter != state_->context_values_.end()) {
    return_context_value = iter->second;
  }

  return return_context_value;
}

void
Session::setContextValue(const std::string &key, std::shared_ptr<Session::ContextValue> value)
{
  state_->context_values_[key] = std::move(value);
}

const sockaddr *
Session::getIncomingAddress() const
{
  return TSHttpSsnIncomingAddrGet(state_->ssn_);
}

const sockaddr *
Session::getClientAddress() const
{
  return TSHttpSsnClientAddrGet(state_->ssn_);
}
