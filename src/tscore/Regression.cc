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

#include "tscore/Regression.h"
#include "tscore/I_Version.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_args.h"

static RegressionTest *test           = nullptr;
static RegressionTest *exclusive_test = nullptr;

RegressionTest *RegressionTest::current = nullptr;
int RegressionTest::ran_tests           = 0;
DFA RegressionTest::dfa;
int RegressionTest::final_status = REGRESSION_TEST_PASSED;

static const char *
progname(const char *path)
{
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

const char *
regression_status_string(int status)
{
  return (status == REGRESSION_TEST_NOT_RUN ?
            "NOT_RUN" :
            (status == REGRESSION_TEST_PASSED ? "PASSED" : (status == REGRESSION_TEST_INPROGRESS ? "INPROGRESS" : "FAILED")));
}

RegressionTest::RegressionTest(const char *_n, const SourceLocation &_l, TestFunction *_f, int _o)
  : name(_n), location(_l), function(_f), next(nullptr), status(REGRESSION_TEST_NOT_RUN), printed(false), opt(_o)
{
  if (opt == REGRESSION_OPT_EXCLUSIVE) {
    if (exclusive_test) {
      this->next = exclusive_test;
    }
    exclusive_test = this;
  } else {
    if (test) {
      this->next = test;
    }
    test = this;
  }
}

static inline int
start_test(RegressionTest *t, int regression_level)
{
  ink_assert(t->status == REGRESSION_TEST_NOT_RUN);
  t->status = REGRESSION_TEST_INPROGRESS;
  fprintf(stderr, "REGRESSION TEST %s started\n", t->name);
  (*t->function)(t, regression_level, &t->status);
  int tresult = t->status;
  if (tresult != REGRESSION_TEST_INPROGRESS) {
    fprintf(stderr, "    REGRESSION_RESULT %s:%*s %s\n", t->name, 40 - static_cast<int>(strlen(t->name)), " ",
            regression_status_string(tresult));
    t->printed = true;
  }
  return tresult;
}

int
RegressionTest::run(const char *atest, int regression_level)
{
  if (atest) {
    dfa.compile(atest);
  } else {
    dfa.compile(".*");
  }

  fprintf(stderr, "REGRESSION_TEST initialization begun\n");
  // start the non exclusive tests
  for (RegressionTest *t = test; t; t = t->next) {
    if ((dfa.match(t->name) >= 0)) {
      int res = start_test(t, regression_level);
      if (res == REGRESSION_TEST_FAILED) {
        final_status = REGRESSION_TEST_FAILED;
      }
    }
  }

  current = exclusive_test;
  return run_some(regression_level);
}

void
RegressionTest::list()
{
  char buf[128];
  const char *bold   = "\x1b[1m";
  const char *unbold = "\x1b[0m";

  if (!isatty(fileno(stdout))) {
    bold = unbold = "";
  }

  for (RegressionTest *t = test; t; t = t->next) {
    fprintf(stdout, "%s%s%s %s\n", bold, t->name, unbold, t->location.str(buf, sizeof(buf)));
  }

  for (RegressionTest *t = exclusive_test; t; t = t->next) {
    fprintf(stdout, "%s%s%s %s\n", bold, t->name, unbold, t->location.str(buf, sizeof(buf)));
  }
}

int
RegressionTest::run_some(int regression_level)
{
  if (current) {
    if (current->status == REGRESSION_TEST_INPROGRESS) {
      return REGRESSION_TEST_INPROGRESS;
    }

    if (current->status != REGRESSION_TEST_NOT_RUN) {
      if (!current->printed) {
        current->printed = true;
        fprintf(stderr, "    REGRESSION_RESULT %s:%*s %s\n", current->name, 40 - static_cast<int>(strlen(current->name)), " ",
                regression_status_string(current->status));
      }
      current = current->next;
    }
  }

  for (; current; current = current->next) {
    if ((dfa.match(current->name) >= 0)) {
      int res = start_test(current, regression_level);
      if (res == REGRESSION_TEST_INPROGRESS) {
        return res;
      }
      if (res == REGRESSION_TEST_FAILED) {
        final_status = REGRESSION_TEST_FAILED;
      }
    }
  }
  return REGRESSION_TEST_INPROGRESS;
}

int
RegressionTest::check_status(int regression_level)
{
  int status = REGRESSION_TEST_PASSED;
  if (current) {
    status = run_some(regression_level);
    if (!current) {
      return status;
    }
  }

  RegressionTest *t = test;
  int exclusive     = 0;

check_test_list:
  while (t) {
    if ((t->status == REGRESSION_TEST_PASSED || t->status == REGRESSION_TEST_FAILED) && !t->printed) {
      t->printed = true;
      fprintf(stderr, "    REGRESSION_RESULT %s:%*s %s\n", t->name, 40 - static_cast<int>(strlen(t->name)), " ",
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
RegressionTest::main(int /* argc */, const char **argv, int level)
{
  char regression_test[1024] = "";
  int regression_list        = 0;
  int regression_level       = level;

  const ArgumentDescription argument_descriptions[] = {
    {"regression", 'R', "Regression Level (quick:1..long:3)", "I", &regression_level, "PROXY_REGRESSION", nullptr},
    {"regression_test", 'r', "Run Specific Regression Test", "S512", regression_test, "PROXY_REGRESSION_TEST", nullptr},
    {"regression_list", 'l', "List Regression Tests", "T", &regression_list, "PROXY_REGRESSION_LIST", nullptr},
  };

  AppVersionInfo version;

  version.setup(PACKAGE_NAME, progname(argv[0]), PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  process_args(&version, argument_descriptions, countof(argument_descriptions), argv);

  if (regression_list) {
    RegressionTest::list();
  } else {
    RegressionTest::run(*regression_test == '\0' ? nullptr : regression_test, regression_level);
  }

  return RegressionTest::final_status == REGRESSION_TEST_PASSED ? 0 : 1;
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
  if (!test) {
    *status = REGRESSION_TEST_FAILED;
  } else {
    *status = REGRESSION_TEST_PASSED;
  }
}
