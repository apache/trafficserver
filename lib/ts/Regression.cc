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

/****************************************************************************

  Regression.cc


 ****************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_assert.h"
#include "ts/Regression.h"

static RegressionTest *test           = NULL;
static RegressionTest *exclusive_test = NULL;

RegressionTest *RegressionTest::current = 0;
int RegressionTest::ran_tests           = 0;
DFA RegressionTest::dfa;
int regression_level             = 0;
int RegressionTest::final_status = REGRESSION_TEST_PASSED;

char *
regression_status_string(int status)
{
  return (
    char *)(status == REGRESSION_TEST_NOT_RUN ?
              "NOT_RUN" :
              (status == REGRESSION_TEST_PASSED ? "PASSED" : (status == REGRESSION_TEST_INPROGRESS ? "INPROGRESS" : "FAILED")));
}

RegressionTest::RegressionTest(const char *name_arg, TestFunction *function_arg, int aopt)
{
  name     = name_arg;
  function = function_arg;
  status   = REGRESSION_TEST_NOT_RUN;
  printed  = 0;
  opt      = aopt;

  if (opt == REGRESSION_OPT_EXCLUSIVE) {
    if (exclusive_test)
      this->next   = exclusive_test;
    exclusive_test = this;
  } else {
    if (test)
      this->next = test;
    test         = this;
  }
}

static inline int
start_test(RegressionTest *t)
{
  ink_assert(t->status == REGRESSION_TEST_NOT_RUN);
  t->status = REGRESSION_TEST_INPROGRESS;
  fprintf(stderr, "REGRESSION TEST %s started\n", t->name);
  (*t->function)(t, regression_level, &t->status);
  int tresult = t->status;
  if (tresult != REGRESSION_TEST_INPROGRESS) {
    fprintf(stderr, "    REGRESSION_RESULT %s:%*s %s\n", t->name, 40 - (int)strlen(t->name), " ",
            regression_status_string(tresult));
    t->printed = 1;
  }
  return tresult;
}

int
RegressionTest::run(const char *atest)
{
  if (atest)
    dfa.compile(atest);
  else
    dfa.compile(".*");
  fprintf(stderr, "REGRESSION_TEST initialization begun\n");
  // start the non exclusive tests
  for (RegressionTest *t = test; t; t = t->next) {
    if ((dfa.match(t->name) >= 0)) {
      int res = start_test(t);
      if (res == REGRESSION_TEST_FAILED)
        final_status = REGRESSION_TEST_FAILED;
    }
  }
  current = exclusive_test;
  return run_some();
}

int
RegressionTest::run_some()
{
  if (current) {
    if (current->status == REGRESSION_TEST_INPROGRESS)
      return REGRESSION_TEST_INPROGRESS;
    else if (current->status != REGRESSION_TEST_NOT_RUN) {
      if (!current->printed) {
        current->printed = true;
        fprintf(stderr, "    REGRESSION_RESULT %s:%*s %s\n", current->name, 40 - (int)strlen(current->name), " ",
                regression_status_string(current->status));
      }
      current = current->next;
    }
  }

  for (; current; current = current->next) {
    if ((dfa.match(current->name) >= 0)) {
      int res = start_test(current);
      if (res == REGRESSION_TEST_INPROGRESS)
        return res;
      if (res == REGRESSION_TEST_FAILED)
        final_status = REGRESSION_TEST_FAILED;
    }
  }
  return REGRESSION_TEST_INPROGRESS;
}

int
RegressionTest::check_status()
{
  int status = REGRESSION_TEST_PASSED;
  if (current) {
    status = run_some();
    if (!current)
      return status;
  }
  RegressionTest *t = test;
  int exclusive     = 0;

check_test_list:
  while (t) {
    if ((t->status == REGRESSION_TEST_PASSED || t->status == REGRESSION_TEST_FAILED) && !t->printed) {
      t->printed = true;
      fprintf(stderr, "    REGRESSION_RESULT %s:%*s %s\n", t->name, 40 - (int)strlen(t->name), " ",
              regression_status_string(t->status));
    }

    switch (t->status) {
    case REGRESSION_TEST_FAILED:
      final_status = REGRESSION_TEST_FAILED;
      break;
    case REGRESSION_TEST_INPROGRESS:
      printf("Regression test(%s) still in progress\n", t->name);
      status = REGRESSION_TEST_INPROGRESS;
      break;
    default:
      break;
    }
    t = t->next;
  }
  if (!exclusive) {
    exclusive = 1;
    t         = exclusive_test;
    goto check_test_list;
  }

  return (status == REGRESSION_TEST_INPROGRESS) ? REGRESSION_TEST_INPROGRESS : final_status;
}

int
rprintf(RegressionTest *t, const char *format, ...)
{
  int l;
  char buffer[8192];

  snprintf(buffer, sizeof(buffer), "RPRINT %s: ", t->name);
  fputs(buffer, stderr);

  va_list ap;
  va_start(ap, format);
  l = vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);

  fputs(buffer, stderr);
  return (l);
}

int
rperf(RegressionTest *t, const char *tag, double val)
{
  int l;
  char format2[8192];
  l = snprintf(format2, sizeof(format2), "RPERF %s.%s %f\n", t->name, tag, val);
  fputs(format2, stderr);
  return (l);
}

REGRESSION_TEST(Regression)(RegressionTest *t, int atype, int *status)
{
  (void)t;
  (void)atype;
  rprintf(t, "regression test\n");
  rperf(t, "speed", 100.0);
  if (!test)
    *status = REGRESSION_TEST_FAILED;
  else
    *status = REGRESSION_TEST_PASSED;
}
