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

#include <vector>
#include <utility>
#include <sstream>

#include <ts/ts.h>

#include "tscpp/util/TextView.h"

#include "tscpp/api/Continuation.h"

// TSReleaseAssert() doesn't seem to produce any logging output for a debug build, so do both kinds of assert.
//
#define ALWAYS_ASSERT(EXPR) \
  {                         \
    TSAssert(EXPR);         \
    TSReleaseAssert(EXPR);  \
  }

std::vector<void (*)()> testList;

struct ATest {
  ATest(void (*testFuncPtr)()) { testList.push_back(testFuncPtr); }
};

// Put a test function, whose name is the actual parameter for TEST_FUNC, into testList, the list of test functions.
//
#define TEST(TEST_FUNC) ATest t(TEST_FUNC);

// TextView test. This is not testing the actual TextView code, just that it works to call functions in TextView.cc in the core
// from a plugin.
//
namespace TextViewTest
{
void
f()
{
  ts::TextView tv("abcdefg");

  std::ostringstream oss;

  oss << tv;

  ALWAYS_ASSERT(memcmp(ts::TextView(oss.str()), tv) == 0)
}

TEST(f)

} // end namespace TextViewTest

// Test for Continuation class.
//
namespace ContinuationTest
{
struct {
  TSEvent event;
  void *edata;
} passedToEventFunc;

bool
checkPassed(TSEvent event, void *edata)
{
  return (passedToEventFunc.event == event) and (passedToEventFunc.edata == edata);
}

class TestCont : public atscppapi::Continuation
{
public:
  TestCont(Mutex m) : atscppapi::Continuation(m) {}

  TestCont() = default;

private:
  int
  _run(TSEvent event, void *edata) override
  {
    passedToEventFunc.event = event;
    passedToEventFunc.edata = edata;

    return 666;
  }
};

void
f()
{
  TestCont::Mutex m(TSMutexCreate());

  TestCont c(m);

  ALWAYS_ASSERT(!!c)
  ALWAYS_ASSERT(c.asTSCont() != nullptr)
  ALWAYS_ASSERT(c.mutex() == m)

  TestCont c2(std::move(c));

  ALWAYS_ASSERT(!!c2)
  ALWAYS_ASSERT(c2.asTSCont() != nullptr)
  ALWAYS_ASSERT(c2.mutex() == m)

  ALWAYS_ASSERT(!c)
  ALWAYS_ASSERT(c.asTSCont() == nullptr)
  ALWAYS_ASSERT(c.mutex() == nullptr)

  TestCont c3;

  ALWAYS_ASSERT(!c3)
  ALWAYS_ASSERT(c3.asTSCont() == nullptr)
  ALWAYS_ASSERT(c3.mutex() == nullptr)

  c3 = std::move(c2);

  ALWAYS_ASSERT(!!c3)
  ALWAYS_ASSERT(c3.asTSCont() != nullptr)
  ALWAYS_ASSERT(c3.mutex() == m)

  ALWAYS_ASSERT(!c2)
  ALWAYS_ASSERT(c2.asTSCont() == nullptr)
  ALWAYS_ASSERT(c2.mutex() == nullptr)

  c3.destroy();

  ALWAYS_ASSERT(!c3)
  ALWAYS_ASSERT(c3.asTSCont() == nullptr)
  ALWAYS_ASSERT(c3.mutex() == nullptr)

  c = TestCont(m);

  ALWAYS_ASSERT(!!c)
  ALWAYS_ASSERT(c.asTSCont() != nullptr)
  ALWAYS_ASSERT(c.mutex() == m)

  ALWAYS_ASSERT(c.call(TS_EVENT_INTERNAL_206) == 666)
  ALWAYS_ASSERT(checkPassed(TS_EVENT_INTERNAL_206, nullptr))

  int dummy;

  ALWAYS_ASSERT(c.call(TS_EVENT_INTERNAL_207, &dummy) == 666)
  ALWAYS_ASSERT(checkPassed(TS_EVENT_INTERNAL_207, &dummy))
}

TEST(f)

} // end namespace ContinuationTest

// Run all the tests.
//
void
TSPluginInit(int, const char **)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = "test_cppapi";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  ALWAYS_ASSERT(TSPluginRegister(&info) == TS_SUCCESS)

  for (auto fp : testList) {
    fp();
  }
}
