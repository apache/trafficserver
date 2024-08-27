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

namespace cripts
{

void
Error::Execute(cripts::Context *context)
{
  if (Failed()) {
    TSHttpTxnStatusSet(context->state.txnp, _status._getter());
    // ToDo: So we can't set the reason phrase here, because ATS doesn't have that
    // as a transaction API, only on the response header...
  }
}

// These are static, to be used with the set() wrapper define
void
Error::Reason::_set(cripts::Context *context, const cripts::string_view msg)
{
  context->state.error.Fail();
  context->state.error._reason._setter(msg);
}

void
Error::Status::_set(cripts::Context *context, TSHttpStatus status)
{
  context->state.error.Fail();
  context->state.error._status._setter(status);
}

TSHttpStatus
Error::Status::_get(cripts::Context *context)
{
  return context->state.error._status._getter();
}

} // namespace cripts
