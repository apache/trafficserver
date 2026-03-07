/*
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

//////////////////////////////////////////////////////////////////////////////////////////////
// operator.cc: Implementation of the operator base class
//
//
#include "ts/ts.h"
#include "operator.h"

OperModifiers
Operator::get_oper_modifiers() const
{
  if (_next) {
    return static_cast<OperModifiers>(_mods | static_cast<Operator *>(_next)->get_oper_modifiers());
  }

  return _mods;
}

void
Operator::initialize(Parser &p)
{
  Statement::initialize(p);

  if (p.consume_mod("L") || p.consume_mod("LAST")) {
    _mods = static_cast<OperModifiers>(_mods | OPER_LAST);
  }

  if (p.consume_mod("QSA")) {
    _mods = static_cast<OperModifiers>(_mods | OPER_QSA);
  }

  if (p.consume_mod("I") || p.consume_mod("INV")) {
    _mods = static_cast<OperModifiers>(_mods | OPER_INV);
  }

  p.validate_mods();
}

void
Operator::initialize(const hrw::OperatorSpec &spec)
{
  initialize_hooks();

  if (need_txn_slot()) {
    _txn_slot = acquire_txn_slot();
  }
  if (need_txn_private_slot()) {
    _txn_private_slot = acquire_txn_private_slot();
  }

  if (spec.mod_last) {
    _mods = static_cast<OperModifiers>(_mods | OPER_LAST);
  }

  if (spec.mod_qsa) {
    _mods = static_cast<OperModifiers>(_mods | OPER_QSA);
  }

  if (spec.mod_inv) {
    _mods = static_cast<OperModifiers>(_mods | OPER_INV);
  }
}

void
OperatorHeaders::initialize(Parser &p)
{
  Operator::initialize(p);

  _header     = p.get_arg();
  _header_wks = TSMimeHdrStringToWKS(_header.c_str(), _header.length());

  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
  require_resources(RSRC_SERVER_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
}

void
OperatorCookies::initialize(Parser &p)
{
  Operator::initialize(p);

  _cookie = p.get_arg();

  require_resources(RSRC_SERVER_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
}
