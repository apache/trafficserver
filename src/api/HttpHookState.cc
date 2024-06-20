/** @file

  Internal SDK stuff

  @section license License

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

#include "api/InkAPIInternal.h"

namespace
{

DbgCtl dbg_ctl_plugin{"plugin"};

} // end anonymous namespace

void
HttpHookState::init(TSHttpHookID id, HttpAPIHooks const *global, HttpAPIHooks const *ssn, HttpAPIHooks const *txn)
{
  _id = id;

  if (global) {
    _global.init(global, id);
  } else {
    _global.clear();
  }

  if (ssn) {
    _ssn.init(ssn, id);
  } else {
    _ssn.clear();
  }

  if (txn) {
    _txn.init(txn, id);
  } else {
    _txn.clear();
  }
}

APIHook const *
HttpHookState::getNext()
{
  APIHook const *zret = nullptr;

  Dbg(dbg_ctl_plugin, "computing next callback for hook %d", _id);

  if (zret = _global.candidate(); zret) {
    ++_global;
  } else if (zret = _ssn.candidate(); zret) {
    ++_ssn;
  } else if (zret = _txn.candidate(); zret) {
    ++_txn;
  }

  return zret;
}

void
HttpHookState::Scope::init(HttpAPIHooks const *feature_hooks, TSHttpHookID id)
{
  _hooks = (*feature_hooks)[id];

  _p = nullptr;
  _c = _hooks->head();
}

APIHook const *
HttpHookState::Scope::candidate()
{
  /// Simply returns _c hook for now. Later will do priority checking here

  // Check to see if a hook has been added since this was initialized empty
  if (nullptr == _c && nullptr == _p && _hooks != nullptr) {
    _c = _hooks->head();
  }
  return _c;
}

void
HttpHookState::Scope::operator++()
{
  _p = _c;
  _c = _c->next();
}

void
HttpHookState::Scope::clear()
{
  _hooks = nullptr;
  _p = _c = nullptr;
}
