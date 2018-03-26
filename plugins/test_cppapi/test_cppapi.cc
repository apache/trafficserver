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

#include <sstream>
#include <vector>

#include <ts/ts.h>

#include <ts/TextView.h>

// TSReleaseAssert() doesn't seem to produce any logging output for a debug build, so do both kinds of assert.
//
#define ASS(EXPR)          \
  {                        \
    TSAssert(EXPR);        \
    TSReleaseAssert(EXPR); \
  }

std::vector<void (*)()> testList;

struct ATest {
  ATest(void (*testFuncPtr)()) { testList.push_back(testFuncPtr); }
};

// Put a test function, whose name is the actual parameter for TEST_FUNC, into testList, the list of test functions.
//
#define TEST(TEST_FUNC) ATest t(TEST_FUNC);

#define TNS_(LN) TestNamespaceName##LN

// Generate a unique name for a separate namespace for each test.
//
#define TNS namespace TNS_(__LINE__)

// TextView test. This is not testing the actual TextView code, just that it works to call functions in TextView.cc in the core
// from a plugin.
//
TNS
{
  void f()
  {
    ts::TextView tv("abcdefg");

    std::ostringstream oss;

    oss << tv;

    ASS(ts::memcmp(ts::TextView(oss.str()), tv) == 0)
  }

  TEST(f)

} // end TNS namespace

// Run all the tests.
//
void
TSPluginInit(int, const char **)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = "test_cppapi";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  ASS(TSPluginRegister(&info) == TS_SUCCESS)

  for (auto fp : testList) {
    fp();
  }
}
