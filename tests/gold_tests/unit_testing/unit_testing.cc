/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <deque>
#include <atomic>
#include <memory>

#include <unistd.h>

#include <tscore/ink_assert.h>

#include "unit_testing.h"

namespace
{
std::deque<void (*)(InProgress)> testList;
}

namespace Au_UT
{
char const Debug_tag[] = "Au_UT";

FileDeleter::FileDeleter(std::string_view pathspec) : _pathspec(pathspec) {}
FileDeleter::~FileDeleter()
{
  unlink(_pathspec.c_str());
}

Test::Test(void (*test_func)(InProgress))
{
  testList.push_back(test_func);
}

} // end namespace Au_UT

namespace
{
// A copy of this is passed to each test function.  If the test creates any self-deleting objects in the heap,
// each such object should contain a copy of this object.  When all the copies are destroyed (or reset), the
// corresponding file will be deleted (and the Au test can detect the deletion).
//
InProgress delete_on_completion;

std::atomic<unsigned> lifecycle_event_count;

int
contFunc(TSCont cont, TSEvent event, void *eventData)
{
  ink_release_assert((TS_EVENT_LIFECYCLE_PORTS_READY == event) || (TS_EVENT_LIFECYCLE_TASK_THREADS_READY == event));

  ++lifecycle_event_count;

  ink_release_assert(lifecycle_event_count <= 2);

  if (2 == lifecycle_event_count) {
    // Run all of the tests in the list.
    //
    for (auto fp : testList) {
      fp(delete_on_completion);
    }

    // Reset the shared pointer.  From now on, the file to be deleted on completion will only continue to exist
    // as long as previously-made copies of this object exist.
    //
    delete_on_completion.reset();

    TSContDestroy(cont);
  }

  return 0;
}

} // end anonymous namespace

// argv[1] - Pathspec of file to delete when all activity triggered by ports ready lifecycle hook completes.
//
void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(Debug_tag, "unit_testing: TSPluginInit()");

  TSReleaseAssert(2 == argc);

  TSPluginRegistrationInfo info;

  info.plugin_name   = "unit_testing";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("unit_testing: Plugin registration failed");

    return;
  }

  delete_on_completion = std::make_shared<FileDeleter>(argv[1]);

  auto cont = TSContCreate(contFunc, nullptr);
  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_READY_HOOK, cont);
  TSLifecycleHookAdd(TS_LIFECYCLE_TASK_THREADS_READY_HOOK, cont);
}
