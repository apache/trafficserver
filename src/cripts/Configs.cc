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

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"

integer
IntConfig::_get(Cript::Context *context) const
{
  integer value = -1;

  TSAssert(context->state.txnp);
  if (TSHttpTxnConfigIntGet(context->state.txnp, _key, &value) != TS_SUCCESS) {
    context->state.error.fail();
  }

  return value;
}

void
IntConfig::_set(Cript::Context *context, integer value)
{
  TSAssert(context->state.txnp);
  if (TSHttpTxnConfigIntSet(context->state.txnp, _key, static_cast<TSMgmtInt>(value)) != TS_SUCCESS) {
    context->state.error.fail();
  }
}

float
FloatConfig::_get(Cript::Context *context) const
{
  float value = -1;

  TSAssert(context->state.txnp);
  if (TSHttpTxnConfigFloatGet(context->state.txnp, _key, &value) != TS_SUCCESS) {
    context->state.error.fail();
  }

  return value;
}

void
FloatConfig::_set(Cript::Context *context, float value)
{
  TSAssert(context->state.txnp);
  if (TSHttpTxnConfigFloatSet(context->state.txnp, _key, static_cast<TSMgmtFloat>(value)) != TS_SUCCESS) {
    context->state.error.fail();
  }
}
