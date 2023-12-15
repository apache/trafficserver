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

void
Error::execute(Cript::Context *context)
{
  if (failed()) {
    TSHttpTxnStatusSet(context->state.txnp, _status.status());
  }
}

// These are static, to be used with the set() wrapper define
void
Error::Message::_set(Cript::Context *context, const Cript::string_view msg)
{
  context->state.error.fail();
  context->state.error._message.setter(msg);
}

void
Error::Status::_set(Cript::Context *context, TSHttpStatus status)
{
  context->state.error.fail();
  context->state.error._status.setter(status);
}
