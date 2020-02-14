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

/*
Tests to run in "ports ready" lifecycle hook.

NOTE: no include guard needed, only included once in test_tsapi.cc.
*/

#include <deque>
#include <memory>

#include <ts/ts.h>

namespace PortsReadyHook
{
// Delete file whose path is specified in the constructor when the instance is destroyed.
//
class FileDeleter
{
public:
  FileDeleter(std::string &&pathspec) : _pathspec(pathspec) {}

  ~FileDeleter() { unlink(_pathspec.c_str()); }

private:
  std::string _pathspec;
};

using InProgress = std::shared_ptr<FileDeleter>;

// A copy of this is passed to each test function.  If the test creates any self-deleting objects in the heap,
// each such object should contain a copy of this object.  When all the copies are destroyed (or reset), the
// corresponding file will be deleted (and the Au test can detect the deletion).
//
InProgress delete_on_completion;

std::deque<void (*)(InProgress)> testList;

struct ATest {
  ATest(void (*testFuncPtr)(InProgress)) { testList.push_back(testFuncPtr); }
};

} // namespace PortsReadyHook

// Put a test function, whose name is the actual parameter for TEST_FUNC, into testList, the list of test functions.
//
#define TEST(TEST_FUNC) PortsReadyHook::ATest t(TEST_FUNC);

#include <ts_tcp.h>

#undef TEST

namespace PortsReadyHook
{
int
contFunc(TSCont cont, TSEvent event, void *eventData)
{
  TSReleaseAssert(TS_EVENT_LIFECYCLE_PORTS_READY == event);

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

  return 0;
}

void
init(char const *rm_pathspec_on_completion_of_all_tests)
{
  delete_on_completion.reset(new FileDeleter(rm_pathspec_on_completion_of_all_tests));

  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_READY_HOOK, TSContCreate(contFunc, nullptr));
}

} // namespace PortsReadyHook
