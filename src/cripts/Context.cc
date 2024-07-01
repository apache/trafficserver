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
ClassAllocator<Cript::Context> criptContextAllocator("Cript::Context");

namespace Cript
{

void
Context::reset()
{
  // Clear the initialized headers before calling next hook
  // Note: we don't clear the pristine URL, nor the Remap From/To URLs, they are static.
  //      We also don't clear the client URL, since it's from the RRI.
  if (_client_resp_header.Initialized()) {
    _client_resp_header.Reset();
  }
  if (_server_resp_header.Initialized()) {
    _server_resp_header.Reset();
  }
  if (_client_req_header.Initialized()) {
    _client_req_header.Reset();
  }
  if (_server_req_header.Initialized()) {
    _server_req_header.Reset();
  }

  // Clear the initialized URLs before calling next hook
  if (_pristine_url.Initialized()) {
    _pristine_url.Reset();
  }
  if (_cache_url.Initialized()) {
    _cache_url.Reset();
  }
  if (_parent_url.Initialized()) {
    _parent_url.Reset();
  }
}

Context *
Context::Factory(TSHttpTxn txn_ptr, TSHttpSsn ssn_ptr, TSRemapRequestInfo *rri_ptr, Cript::Instance &inst)
{
  return THREAD_ALLOC(criptContextAllocator, this_thread(), txn_ptr, ssn_ptr, rri_ptr, inst);
}

void
Context::Release()
{
  THREAD_FREE(this, criptContextAllocator, this_thread());
}

} // namespace Cript
