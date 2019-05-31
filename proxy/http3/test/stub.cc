/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

// FIXME: remove stub

#include "I_EventSystem.h"
#include "InkAPIInternal.h"

void
APIHooks::clear()
{
  ink_abort("do not call stub");
}

void
APIHooks::append(INKContInternal *)
{
  ink_abort("do not call stub");
}

void
APIHooks::prepend(INKContInternal *)
{
  ink_abort("do not call stub");
}

#include "HttpDebugNames.h"
const char *
HttpDebugNames::get_api_hook_name(TSHttpHookID t)
{
  return "dummy";
}

#include "HttpSM.h"
HttpSM::HttpSM() : Continuation(nullptr), vc_table(this) {}

void
HttpSM::cleanup()
{
  ink_abort("do not call stub");
}

void
HttpSM::destroy()
{
  ink_abort("do not call stub");
}

void
HttpSM::set_next_state()
{
  ink_abort("do not call stub");
}

void
HttpSM::handle_api_return()
{
  ink_abort("do not call stub");
}

int
HttpSM::kill_this_async_hook(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  return EVENT_DONE;
}

void
HttpSM::attach_client_session(ProxyTransaction *, IOBufferReader *)
{
  ink_abort("do not call stub");
}

void
HttpSM::init()
{
  ink_abort("do not call stub");
}

ClassAllocator<HttpSM> httpSMAllocator("httpSMAllocator");
HttpAPIHooks *http_global_hooks;

HttpVCTable::HttpVCTable(HttpSM *) {}

PostDataBuffers::~PostDataBuffers() {}

HttpTunnel::HttpTunnel() : Continuation(nullptr) {}
HttpTunnelConsumer::HttpTunnelConsumer() {}
HttpTunnelProducer::HttpTunnelProducer() {}
ChunkedHandler::ChunkedHandler() {}

HttpCacheSM::HttpCacheSM() {}

HttpCacheAction::HttpCacheAction() : sm(nullptr) {}
void
HttpCacheAction::cancel(Continuation *c)
{
}
