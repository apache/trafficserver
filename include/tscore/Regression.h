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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/Regex.h"
#include "tscore/Diags.h"

//   Each module should provide one or more regression tests
//
//   An example:
//
//   REGRESSION_TEST(Addition)(RegressionTest *t, int atype, int *pstatus) {
//     if (atype < REGRESSION_TEST_NIGHTLY) { // to expensive to do more than nightly
//       *pstatus = REGRESSION_TEST_NOT_RUN;
//       return;
//     }
//     if (1 + 1 != 2) {
//       rprintf(t, "drat, 1+1 isn't 2??");
//       *pstatus = REGRESSION_TEST_FAILED;
//     } else
//       *pstatus = REGRESSION_TEST_PASSED;
//   }

// status values
#define REGRESSION_TEST_PASSED 1
#define REGRESSION_TEST_INPROGRESS 0 // initial value
#define REGRESSION_TEST_FAILED -1
#define REGRESSION_TEST_NOT_RUN -2

// regression types
#define REGRESSION_TEST_NONE 0
#define REGRESSION_TEST_QUICK 1
#define REGRESSION_TEST_NIGHTLY 2
#define REGRESSION_TEST_EXTENDED 3

// regression options
#define REGRESSION_OPT_EXCLUSIVE (1 << 0)

#define RegressionMakeLocation(f) SourceLocation(__FILE__, f, __LINE__)

struct RegressionTest;

typedef void TestFunction(RegressionTest *t, int type, int *status);

struct RegressionTest {
  const char *name;
  const SourceLocation location;
  TestFunction *function;
  RegressionTest *next;
  int status;
  bool printed;
  int opt;

  RegressionTest(const char *name_arg, const SourceLocation &loc, TestFunction *function_arg, int aopt);

  static int final_status;
  static int ran_tests;
  static DFA dfa;
  static RegressionTest *current;
  static int run(const char *name, int regression_level);
  static void list();
  static int run_some(int regression_level);
  static int check_status(int regression_level);

  static int main(int argc, const char **argv, int level);
};

#define REGRESSION_TEST(_f)                                                                                        \
  void RegressionTest_##_f(RegressionTest *t, int atype, int *pstatus);                                            \
  RegressionTest regressionTest_##_f(#_f, RegressionMakeLocation("RegressionTest_" #_f), &RegressionTest_##_f, 0); \
  void RegressionTest_##_f

#define EXCLUSIVE_REGRESSION_TEST(_f)                                                                          \
  void RegressionTest_##_f(RegressionTest *t, int atype, int *pstatus);                                        \
  RegressionTest regressionTest_##_f(#_f, RegressionMakeLocation("RegressionTest_" #_f), &RegressionTest_##_f, \
                                     REGRESSION_OPT_EXCLUSIVE);                                                \
  void RegressionTest_##_f

int rprintf(RegressionTest *t, const char *format, ...);
int rperf(RegressionTest *t, const char *tag, double val);
const char *regression_status_string(int status);

void tell_diags_regression_testing_is_on();
