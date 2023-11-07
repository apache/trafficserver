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

#include "iocore/net/SSLAPIHooks.h"

#include "api/InkAPIInternal.h"

#include "proxy/HttpAPIHooks.h"

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

namespace tsapi::c
{
TSVConn
TSHttpConnectWithPluginId(sockaddr const *addr, const char *tag, int64_t id)
{
  return TSVConn{};
}

int TS_MIME_LEN_CONTENT_LENGTH           = 0;
const char *TS_MIME_FIELD_CONTENT_LENGTH = "";

TSIOBufferBlock
TSIOBufferReaderStart(TSIOBufferReader readerp)
{
  return TSIOBufferBlock{};
}

TSIOBufferBlock
TSIOBufferBlockNext(TSIOBufferBlock blockp)
{
  return TSIOBufferBlock{};
}

const char *
TSIOBufferBlockReadStart(TSIOBufferBlock blockp, TSIOBufferReader readerp, int64_t *avail)
{
  return "";
}

void
TSIOBufferReaderConsume(TSIOBufferReader readerp, int64_t nbytes)
{
}
} // namespace tsapi::c
