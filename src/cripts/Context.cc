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

void
Cript::Context::reset()
{
  // Clear the initialized headers before calling next hook
  // Note: we don't clear the pristine URL, nor the Remap From/To URLs, they are static.
  //      We also don't clear the client URL, since it's from the RRI.
  if (_client_resp_header.initialized()) {
    _client_resp_header.reset();
  }
  if (_server_resp_header.initialized()) {
    _server_resp_header.reset();
  }
  if (_client_req_header.initialized()) {
    _client_req_header.reset();
  }
  if (_server_req_header.initialized()) {
    _server_req_header.reset();
  }

  // Clear the initialized URLs before calling next hook
  if (_cache_url.initialized()) {
    _cache_url.reset();
  }
  if (_parent_url.initialized()) {
    _parent_url.reset();
  }
}

// Freelist management. These are here to avoid polluting the Context includes with ATS core includes.
ClassAllocator<Cript::Context> criptContextAllocator("Cript::Context");

Cript::Context *
Cript::Context::factory(TSHttpTxn txn_ptr, TSHttpSsn ssn_ptr, TSRemapRequestInfo *rri_ptr, Cript::Instance &inst)
{
  return THREAD_ALLOC(criptContextAllocator, this_thread(), txn_ptr, ssn_ptr, rri_ptr, inst);
}

void
Cript::Context::release()
{
  THREAD_FREE(this, criptContextAllocator, this_thread());
}
