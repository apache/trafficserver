/** @file

  Multiplexes request to other origins.

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
#include <assert.h>
#include <limits>

#include "post.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

PostState::~PostState()
{
  if (buffer != NULL) {
    TSIOBufferDestroy(buffer);
    buffer = NULL;
  }
}

PostState::PostState(Requests &r) : buffer(NULL), reader(NULL), vio(NULL)
{
  assert(!r.empty());
  requests.swap(r);
}

static void
postTransform(const TSCont c, PostState &s)
{
  assert(c != NULL);

  const TSVConn vconnection = TSTransformOutputVConnGet(c);
  assert(vconnection != NULL);

  const TSVIO vio = TSVConnWriteVIOGet(c);
  assert(vio != NULL);

  if (!s.buffer) {
    s.buffer = TSIOBufferCreate();
    assert(s.buffer != NULL);

    const TSIOBufferReader reader = TSIOBufferReaderAlloc(s.buffer);
    assert(reader != NULL);

    s.reader = TSIOBufferReaderClone(reader);
    assert(s.reader != NULL);

    s.vio = TSVConnWrite(vconnection, c, reader, std::numeric_limits<int64_t>::max());
    assert(s.vio != NULL);
  }

  if (!TSVIOBufferGet(vio)) {
    TSVIONBytesSet(s.vio, TSVIONDoneGet(vio));
    TSVIOReenable(s.vio);
    return;
  }

  int64_t toWrite = TSVIONTodoGet(vio);
  assert(toWrite >= 0);

  if (toWrite > 0) {
    toWrite = std::min(toWrite, TSIOBufferReaderAvail(TSVIOReaderGet(vio)));
    assert(toWrite >= 0);

    if (toWrite > 0) {
      TSIOBufferCopy(TSVIOBufferGet(s.vio), TSVIOReaderGet(vio), toWrite, 0);
      TSIOBufferReaderConsume(TSVIOReaderGet(vio), toWrite);
      TSVIONDoneSet(vio, TSVIONDoneGet(vio) + toWrite);
    }
  }

  if (TSVIONTodoGet(vio) > 0) {
    if (toWrite > 0) {
      TSVIOReenable(s.vio);
      CHECK(TSContCall(TSVIOContGet(vio), TS_EVENT_VCONN_WRITE_READY, vio));
    }
  } else {
    TSVIONBytesSet(s.vio, TSVIONDoneGet(vio));
    TSVIOReenable(s.vio);
    CHECK(TSContCall(TSVIOContGet(vio), TS_EVENT_VCONN_WRITE_COMPLETE, vio));
  }
}

int
handlePost(TSCont c, TSEvent e, void *data)
{
  assert(c != NULL);
  // TODO(dmorilha): assert on possible events.
  PostState *const state = static_cast<PostState *>(TSContDataGet(c));
  assert(state != NULL);
  if (TSVConnClosedGet(c)) {
    assert(data != NULL);
    if (state->reader != NULL) {
      addBody(state->requests, state->reader);
    }
    dispatch(state->requests, timeout);
    delete state;
    TSContDataSet(c, NULL);
    TSContDestroy(c);
    return 0;
  } else {
    switch (e) {
    case TS_EVENT_ERROR: {
      const TSVIO vio = TSVConnWriteVIOGet(c);
      assert(vio != NULL);
      CHECK(TSContCall(TSVIOContGet(vio), TS_EVENT_ERROR, vio));
    } break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSVConnShutdown(TSTransformOutputVConnGet(c), 0, 1);
      break;

    case TS_EVENT_VCONN_WRITE_READY:
    default:
      postTransform(c, *state);
      break;
    }
  }
  return 0;
}
