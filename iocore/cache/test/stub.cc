/** @file

  Stub file for linking libinknet.a from unit tests

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

#include <string_view>

#include "HttpSessionManager.h"
#include "HttpBodyFactory.h"
#include "DiagsConfig.h"
#include "ts/InkAPIPrivateIOCore.h"

#include "tscore/I_Version.h"

AppVersionInfo appVersionInfo;

#include "api/InkAPIInternal.h"
void
APIHooks::append(INKContInternal *cont)
{
}

int
APIHook::invoke(int, void *) const
{
  ink_assert(false);
  return 0;
}

int
APIHook::blocking_invoke(int, void *) const
{
  ink_assert(false);
  return 0;
}

APIHook *
APIHook::next() const
{
  ink_assert(false);
  return nullptr;
}

APIHook *
APIHooks::head() const
{
  return nullptr;
}

void
APIHooks::clear()
{
}

HttpHookState::HttpHookState() {}

void
HttpHookState::init(TSHttpHookID id, HttpAPIHooks const *global, HttpAPIHooks const *ssn, HttpAPIHooks const *txn)
{
}

void
api_init()
{
}

APIHook const *
HttpHookState::getNext()
{
  return nullptr;
}

void
ConfigUpdateCbTable::invoke(const char * /* name ATS_UNUSED */)
{
  ink_release_assert(false);
}

HttpAPIHooks *http_global_hooks        = nullptr;
SslAPIHooks *ssl_hooks                 = nullptr;
LifecycleAPIHooks *lifecycle_hooks     = nullptr;
ConfigUpdateCbTable *global_config_cbs = nullptr;

void
INKVConnInternal::do_io_close(int error)
{
}

void
INKVConnInternal::do_io_shutdown(ShutdownHowTo_t howto)
{
}

VIO *
INKVConnInternal::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  return nullptr;
}

VIO *
INKVConnInternal::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  return nullptr;
}

void
INKVConnInternal::destroy()
{
}

void
INKVConnInternal::free()
{
}

void
INKVConnInternal::clear()
{
}

void
INKVConnInternal::reenable(VIO * /* vio ATS_UNUSED */)
{
}

bool
INKVConnInternal::get_data(int id, void *data)
{
  return false;
}

bool
INKVConnInternal::set_data(int id, void *data)
{
  return false;
}

void
INKVConnInternal::do_io_transform(VConnection *vc)
{
}

void
INKContInternal::handle_event_count(int event)
{
}

void
INKVConnInternal::retry(unsigned int delay)
{
}

INKContInternal::INKContInternal(TSEventFunc funcp, TSMutex mutexp) : DummyVConnection(reinterpret_cast<ProxyMutex *>(mutexp)) {}

INKContInternal::INKContInternal() : DummyVConnection(nullptr) {}

void
INKContInternal::destroy()
{
}

void
INKContInternal::clear()
{
}

void
INKContInternal::free()
{
}

INKVConnInternal::INKVConnInternal() : INKContInternal() {}

INKVConnInternal::INKVConnInternal(TSEventFunc funcp, TSMutex mutexp) : INKContInternal(funcp, mutexp) {}

#include "api/FetchSM.h"
ClassAllocator<FetchSM> FetchSMAllocator("unusedFetchSMAllocator");
void
FetchSM::ext_launch()
{
}
void
FetchSM::ext_destroy()
{
}
ssize_t
FetchSM::ext_read_data(char *, unsigned long)
{
  return 0;
}
void
FetchSM::ext_add_header(char const *, int, char const *, int)
{
}
void
FetchSM::ext_write_data(void const *, unsigned long)
{
}
void *
FetchSM::ext_get_user_data()
{
  return nullptr;
}
void
FetchSM::ext_set_user_data(void *)
{
}
void
FetchSM::ext_init(Continuation *, char const *, char const *, char const *, sockaddr const *, int)
{
}
