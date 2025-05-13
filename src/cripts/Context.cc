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

#include "iocore/eventsystem/EventSystem.h"
#undef Status // Major namespace conflict here with tscore/Diags.h::Status
#undef Error

#include "cripts/Lulu.hpp"
#include "cripts/Context.hpp"

// Freelist management. These are here to avoid polluting the Context includes with ATS core includes.
ClassAllocator<cripts::Context> criptContextAllocator("cripts::Context");

namespace cripts
{

void
Context::reset()
{
  // Clear the initialized headers before calling next hook
  // Note: we don't clear the pristine URL, nor the Remap From/To URLs, they are static.
  //      We also don't clear the client URL, since it's from the RRI.
  if (_client.response.Initialized()) {
    _client.response.Reset();
  }
  if (_server.response.Initialized()) {
    _server.response.Reset();
  }
  if (_client.request.Initialized()) {
    _client.request.Reset();
  }
  if (_server.request.Initialized()) {
    _server.request.Reset();
  }

  // Clear the initialized URLs before calling next hook
  if (_urls.pristine.Initialized()) {
    _urls.pristine.Reset();
  }
  if (_urls.cache.Initialized()) {
    _urls.cache.Reset();
  }
  if (_urls.parent.Initialized()) {
    _urls.parent.Reset();
  }
}

Context *
Context::Factory(TSHttpTxn txn_ptr, TSHttpSsn ssn_ptr, TSRemapRequestInfo *rri_ptr, cripts::Instance &inst)
{
  return THREAD_ALLOC(criptContextAllocator, this_thread(), txn_ptr, ssn_ptr, rri_ptr, inst);
}

void
Context::Release()
{
  THREAD_FREE(this, criptContextAllocator, this_thread());
}

} // namespace cripts
