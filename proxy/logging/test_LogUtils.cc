/** @file

  A brief file description

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

#include "tscore/TestBox.h"
#include "LogUtils.cc"
#include <string.h>

#if 0
// Stub
EThread *
this_ethread()
{
  return nullptr;
}
#endif

REGRESSION_TEST(LogUtils_pure_escapify_url)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  char input[][32] = {
    " ",
    "%",
    "% ",
    "%20",
  };
  const char *expected[] = {
    "%20",
    "%25",
    "%25%20",
    "%2520",
  };
  char output[128];
  int output_len;

  int n = sizeof(input) / sizeof(input[0]);
  for (int i = 0; i < n; ++i) {
    LogUtils::pure_escapify_url(NULL, input[i], strlen(input[i]), &output_len, output, 128);
    box.check(strcmp(output, expected[i]) == 0, "expected \"%s\", was \"%s\"", expected[i], output);
  }
}

REGRESSION_TEST(LogUtils_escapify_url)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  char input[][32] = {
    " ",
    "%",
    "% ",
    "%20",
  };
  const char *expected[] = {
    "%20",
    "%25",
    "%25%20",
    "%20",
  };
  char output[128];
  int output_len;

  int n = sizeof(input) / sizeof(input[0]);
  for (int i = 0; i < n; ++i) {
    LogUtils::escapify_url(NULL, input[i], strlen(input[i]), &output_len, output, 128);
    box.check(strcmp(output, expected[i]) == 0, "expected \"%s\", was \"%s\"", expected[i], output);
  }
}

int
main(int argc, const char **argv)
{
  return RegressionTest::main(argc, argv, REGRESSION_TEST_QUICK);
}

//
// Stub
//
void
RecSignalManager(int, const char *, unsigned long)
{
  ink_release_assert(false);
}
