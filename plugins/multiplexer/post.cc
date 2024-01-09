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
#include <cassert>
#include <limits>

#include "post.h"
#include "ts/ts.h"
#include "tsutil/DbgCtl.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

using multiplexer_ns::dbg_ctl;

PostState::~PostState()
{
  if (origin_buffer != nullptr) {
    TSIOBufferDestroy(origin_buffer);
    origin_buffer = nullptr;
  }
}

PostState::PostState(Requests &r, int content_length)
  : content_length{content_length}, origin_buffer(nullptr), clone_reader(nullptr), output_vio(nullptr)
{
  assert(!r.empty());
  requests.swap(r);
}

static void
postTransform(const TSCont c, PostState &s)
{
  assert(c != nullptr);

  // As we collect data from the client, we need to write it to the origin. This
  // is for the original origin. The copies are handled via HttpTransaction
  // logic in fetcher.h.
  const TSVConn output_vconn = TSTransformOutputVConnGet(c);
  assert(output_vconn != nullptr);

  // The VIO from which we pull out the client's request.
  const TSVIO input_vio = TSVConnWriteVIOGet(c);
  assert(input_vio != nullptr);

  if (!s.origin_buffer) {
    s.origin_buffer = TSIOBufferCreate();
    assert(s.origin_buffer != nullptr);

    TSIOBufferReader origin_reader = TSIOBufferReaderAlloc(s.origin_buffer);
    assert(origin_reader != nullptr);

    s.clone_reader = TSIOBufferReaderClone(origin_reader);
    assert(s.clone_reader != nullptr);

    // A future patch should support chunked POST bodies. In those cases, we
    // can use INT64_MAX instead of s.content_length.
    assert(s.content_length > 0);
    s.output_vio = TSVConnWrite(output_vconn, c, origin_reader, s.content_length);
    assert(s.output_vio != nullptr);
  }

  if (!TSVIOBufferGet(input_vio)) {
    if (s.output_vio) {
      // This indicates that the request is done.
      TSVIONBytesSet(s.output_vio, TSVIONDoneGet(input_vio));
      TSVIOReenable(s.output_vio);
    } else {
      Dbg(dbg_ctl, "PostState::postTransform no input nor output VIO. Returning.");
    }
    return;
  }

  int64_t toWrite = TSVIONTodoGet(input_vio);
  assert(toWrite >= 0);

  if (toWrite > 0) {
    toWrite = std::min(toWrite, TSIOBufferReaderAvail(TSVIOReaderGet(input_vio)));
    assert(toWrite >= 0);

    if (toWrite > 0) {
      TSIOBufferCopy(TSVIOBufferGet(s.output_vio), TSVIOReaderGet(input_vio), toWrite, 0);
      TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), toWrite);
      TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + toWrite);
    }
  }

  if (TSVIONTodoGet(input_vio) > 0) {
    if (toWrite > 0) {
      assert(s.output_vio != nullptr);
      TSVIOReenable(s.output_vio);
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
    }
  } else {
    TSVIONBytesSet(s.output_vio, TSVIONDoneGet(input_vio));
    TSVIOReenable(s.output_vio);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }
}

int
handlePost(TSCont c, TSEvent e, void *data)
{
  assert(c != nullptr);
  // TODO(dmorilha): assert on possible events.
  PostState *const state = static_cast<PostState *>(TSContDataGet(c));
  assert(state != nullptr);
  if (TSVConnClosedGet(c)) {
    assert(data != nullptr);
    if (state->clone_reader != nullptr) {
      addBody(state->requests, state->clone_reader);
    }
    dispatch(state->requests, timeout);
    delete state;
    TSContDataSet(c, nullptr);
    TSContDestroy(c);
    return 0;
  } else {
    switch (e) {
    case TS_EVENT_ERROR: {
      const TSVIO vio = TSVConnWriteVIOGet(c);
      assert(vio != nullptr);
      TSContCall(TSVIOContGet(vio), TS_EVENT_ERROR, vio);
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
