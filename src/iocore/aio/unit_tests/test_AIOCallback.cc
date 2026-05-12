/** @file

  Catch based unit tests for AIOCallback.

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

#include <catch2/catch_test_macros.hpp>

#include "iocore/aio/AIO.h"
#include "iocore/eventsystem/Event.h"

namespace
{

struct AIOCompletionOwner : Continuation {
  AIOCallback callback;
  bool       *completed = nullptr;

  explicit AIOCompletionOwner(bool &completion_flag) : Continuation(nullptr), completed(&completion_flag)
  {
    SET_HANDLER(&AIOCompletionOwner::handle_aio_complete);

    callback.action           = this;
    callback.aiocb.aio_nbytes = 0;
    callback.aio_result       = 0;
  }

  int
  handle_aio_complete(int event, void *data)
  {
    CHECK(event == AIO_EVENT_DONE);
    CHECK(data == &callback);

    *completed = true;
    delete this;
    return EVENT_DONE;
  }
};

struct AIOCompletionHandler : Continuation {
  AIOCallback *expected  = nullptr;
  bool        *completed = nullptr;

  explicit AIOCompletionHandler(bool &completion_flag) : Continuation(nullptr), completed(&completion_flag)
  {
    SET_HANDLER(&AIOCompletionHandler::handle_aio_complete);
  }

  int
  handle_aio_complete(int event, void *data)
  {
    CHECK(event == AIO_EVENT_DONE);
    CHECK(data == expected);

    *completed = true;
    return EVENT_DONE;
  }
};

struct DeletionTrackedAIOCallback : AIOCallback {
  bool *destroyed = nullptr;

  explicit DeletionTrackedAIOCallback(bool &destroyed_flag) : destroyed(&destroyed_flag) {}

  ~DeletionTrackedAIOCallback() override { *destroyed = true; }
};

} // namespace

TEST_CASE("AIOCallback completion tolerates owner deletion", "[iocore][aio]")
{
  bool completed = false;
  auto owner     = new AIOCompletionOwner(completed);
  auto callback  = &owner->callback;

  // Without ASan, a broken implementation can still pass because the stale value of from_ts_api is typically false.
  CHECK(callback->io_complete(EVENT_NONE, nullptr) == EVENT_DONE);
  CHECK(completed);
}

TEST_CASE("API AIOCallback completion deletes the callback after dispatch", "[iocore][aio]")
{
  bool completed = false;
  bool destroyed = false;

  AIOCompletionHandler handler(completed);
  auto                *callback = new DeletionTrackedAIOCallback(destroyed);

  handler.expected           = callback;
  callback->action           = &handler;
  callback->from_ts_api      = true;
  callback->aiocb.aio_nbytes = 0;
  callback->aio_result       = 0;

  CHECK(callback->io_complete(EVENT_NONE, nullptr) == EVENT_DONE);
  CHECK(completed);
  CHECK(destroyed);
}
