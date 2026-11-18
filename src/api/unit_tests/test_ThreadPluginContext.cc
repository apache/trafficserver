/** @file

  Unit tests for the plugin context of a thread from TSThreadCreate.

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

#include "proxy/PluginThreadContext.h"
#include "ts/ts.h"

#include <atomic>

#include <catch2/catch_test_macros.hpp>

extern thread_local PluginThreadContext *pluginThreadContext;

namespace
{
struct TestContext : PluginThreadContext {
  std::atomic<int> acquired = 0;
  std::atomic<int> released = 0;

  void
  acquire() override
  {
    ++acquired;
  }
  void
  release() override
  {
    ++released;
  }
};

struct ThreadResult {
  PluginThreadContext *context            = nullptr;
  int                  released_in_thread = -1;
  TestContext         *expected           = nullptr;
};

void *
record_context(void *data)
{
  auto *result               = static_cast<ThreadResult *>(data);
  result->context            = pluginThreadContext;
  result->released_in_thread = result->expected->released;
  return nullptr;
}
} // namespace

TEST_CASE("A thread from TSThreadCreate has the plugin context of its creator", "[api][thread]")
{
  TestContext  context;
  ThreadResult result;
  result.expected = &context;

  pluginThreadContext = &context;
  TSThread thread     = TSThreadCreate(record_context, &result);
  pluginThreadContext = nullptr;

  REQUIRE(thread != nullptr);
  TSThreadWait(thread);
  TSThreadDestroy(thread);

  CHECK(result.context == &context);
  CHECK(result.released_in_thread == 0);
  CHECK(context.acquired == 1);
  CHECK(context.released == 1);
}
