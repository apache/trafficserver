/** @file

  Inlines base64 images from the ATS cache

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
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <cinttypes>
#include <limits>
#include <cstdio>
#include <unistd.h>

#include "inliner-handler.h"
#include "ts.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

// disable timeout for now
const size_t timeout = 0;

struct MyData {
  ats::inliner::Handler handler;

  MyData(const TSIOBufferReader r, const TSVConn v)
    : handler(r, ats::io::IOSink::Create(TSTransformOutputVConnGet(v), TSContMutexGet(v), timeout))
  {
    assert(r != nullptr);
    assert(v != nullptr);
  }
};

void
handle_transform(const TSCont c)
{
  const TSVIO vio = TSVConnWriteVIOGet(c);

  MyData *const data = static_cast<MyData *>(TSContDataGet(c));

  if (!TSVIOBufferGet(vio)) {
    TSVConnShutdown(c, 1, 0);
    TSContDataSet(c, nullptr);
    delete data;
    return;
  }

  auto todo = TSVIONTodoGet(vio);

  if (todo > 0) {
    const TSIOBufferReader reader = TSVIOReaderGet(vio);
    todo                          = std::min(todo, TSIOBufferReaderAvail(reader));

    if (todo > 0) {
      if (!data) {
        const_cast<MyData *&>(data) = new MyData(TSVIOReaderGet(vio), c);
        TSContDataSet(c, data);
      }

      data->handler.parse();

      TSIOBufferReaderConsume(reader, todo);
      TSVIONDoneSet(vio, TSVIONDoneGet(vio) + todo);
    }
  }

  if (TSVIONTodoGet(vio) > 0) {
    if (todo > 0) {
      TSContCall(TSVIOContGet(vio), TS_EVENT_VCONN_WRITE_READY, vio);
    }
  } else {
    TSContCall(TSVIOContGet(vio), TS_EVENT_VCONN_WRITE_COMPLETE, vio);
    TSVConnShutdown(c, 1, 0);
    TSContDataSet(c, nullptr);
    delete data;
  }
}

int
inliner_transform(TSCont c, TSEvent e, void *)
{
  if (TSVConnClosedGet(c)) {
    TSDebug(PLUGIN_TAG, "connection closed");
    MyData *const data = static_cast<MyData *>(TSContDataGet(c));
    if (data != nullptr) {
      TSContDataSet(c, nullptr);
      data->handler.abort();
      delete data;
    }
    TSContDestroy(c);
  } else {
    switch (e) {
    case TS_EVENT_ERROR: {
      const TSVIO vio = TSVConnWriteVIOGet(c);
      assert(vio != nullptr);
      TSContCall(TSVIOContGet(vio), TS_EVENT_ERROR, vio);
    } break;

    case TS_EVENT_IMMEDIATE:
      handle_transform(c);
      break;

    default:
      TSError("[" PLUGIN_TAG "] Unknown event: %i", e);
      assert(false); // UNREACHABLE
    }
  }

  return 0;
}

bool
transformable(TSHttpTxn txnp)
{
  bool returnValue;
  TSMBuffer buffer;
  TSMLoc location;
  CHECK(TSHttpTxnServerRespGet(txnp, &buffer, &location));
  assert(buffer != nullptr);
  assert(location != nullptr);

  returnValue = TSHttpHdrStatusGet(buffer, location) == TS_HTTP_STATUS_OK;

  if (returnValue) {
    returnValue        = false;
    const TSMLoc field = TSMimeHdrFieldFind(buffer, location, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE);

    if (field != TS_NULL_MLOC) {
      int length                = 0;
      const char *const content = TSMimeHdrFieldValueStringGet(buffer, location, field, 0, &length);

      if (content != nullptr && length > 0) {
        returnValue = strncasecmp(content, "text/html", 9) == 0;
      }

      TSHandleMLocRelease(buffer, location, field);
    }
  }

  CHECK(TSHandleMLocRelease(buffer, TS_NULL_MLOC, location));

  returnValue &= !TSHttpTxnIsInternal(txnp);
  return returnValue;
}

void
transform_add(const TSHttpTxn t)
{
  assert(t != nullptr);
  const TSVConn vconnection = TSTransformCreate(inliner_transform, t);
  assert(vconnection != nullptr);
  TSHttpTxnHookAdd(t, TS_HTTP_RESPONSE_TRANSFORM_HOOK, vconnection);
}

int
transform_plugin(TSCont, TSEvent e, void *d)
{
  assert(TS_EVENT_HTTP_READ_RESPONSE_HDR == e);
  assert(d != nullptr);

  const TSHttpTxn transaction = static_cast<TSHttpTxn>(d);

  switch (e) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (transformable(transaction)) {
      transform_add(transaction);
    }

    TSHttpTxnReenable(transaction, TS_EVENT_HTTP_CONTINUE);
    break;

  default:
    assert(false); // UNRECHEABLE
    break;
  }

  return TS_SUCCESS;
}

void
TSPluginInit(int, const char **)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PLUGIN_TAG);
  info.vendor_name   = const_cast<char *>("MyCompany");
  info.support_email = const_cast<char *>("ts-api-support@MyCompany.com");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[" PLUGIN_TAG "] Plugin registration failed.\n");
    goto error;
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, nullptr));
  return;

error:
  TSError("[null-tranform] Unable to initialize plugin (disabled).\n");
}
