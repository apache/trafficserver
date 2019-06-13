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
Regression testing code for TS API.  Not comprehensive, hopefully will be built up over time.
*/

#include <fstream>
#include <cstdlib>
#include <string_view>
#include <cstdio>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <ts/ts.h>

// TSReleaseAssert() doesn't seem to produce any logging output for a debug build, so do both kinds of assert.
//
#define ALWAYS_ASSERT(EXPR) \
  {                         \
    TSAssert(EXPR);         \
    TSReleaseAssert(EXPR);  \
  }

namespace
{
#define PINAME "test_tsapi"
char PIName[] = PINAME;

// NOTE:  It's important to flush this after writing so that a gold test using this plugin can examine the log before TS
// terminates.
//
std::fstream logFile;

TSCont tCont, gCont;

void testDbgOutputPerformance();

void
testsForReadReqHdrHook(TSHttpTxn txn)
{
  testDbgOutputPerformance();

  logFile << "TSHttpTxnEffectiveUrlStringGet():  ";
  int urlLength;
  char *urlStr = TSHttpTxnEffectiveUrlStringGet(txn, &urlLength);
  if (!urlStr) {
    logFile << "URL null" << std::endl;
  } else if (0 == urlLength) {
    logFile << "URL length zero" << std::endl;
  } else if (0 > urlLength) {
    logFile << "URL length negative" << std::endl;
  } else {
    logFile << std::string_view(urlStr, urlLength) << std::endl;

    TSfree(urlStr);
  }
}

int
transactionContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Transaction: event=" << TSHttpEventNameLookup(event) << std::endl;

  TSDebug(PIName, "Transaction: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    testsForReadReqHdrHook(txn);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    ALWAYS_ASSERT(false)
  } break;

  } // end switch

  return 0;
}

int
globalContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Global: event=" << TSHttpEventNameLookup(event) << std::endl;

  TSDebug(PIName, "Global: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_TXN_START: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    TSHttpTxnHookAdd(txn, TS_HTTP_READ_REQUEST_HDR_HOOK, tCont);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    testsForReadReqHdrHook(txn);

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    ALWAYS_ASSERT(false)
  } break;

  } // end switch

  return 0;
}

} // end anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PIName, "TSPluginInit()");

  TSPluginRegistrationInfo info;

  info.plugin_name   = PIName;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PINAME ": Plugin registration failed");

    return;
  }

  const char *fileSpec = std::getenv("OUTPUT_FILE");

  if (nullptr == fileSpec) {
    TSError(PINAME ": Environment variable OUTPUT_FILE not found.");

    return;
  }

  // Disable output buffering for logFile, so that explicit flushing is not necessary.
  logFile.rdbuf()->pubsetbuf(nullptr, 0);

  logFile.open(fileSpec, std::ios::out);
  if (!logFile.is_open()) {
    TSError(PINAME ": could not open log file \"%s\"", fileSpec);

    return;
  }

  // Mutex to protext the logFile object.
  //
  TSMutex mtx = TSMutexCreate();

  gCont = TSContCreate(globalContFunc, mtx);

  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, gCont);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, gCont);

  tCont = TSContCreate(transactionContFunc, mtx);
}

namespace
{
class Exe_tm
{
public:
  Exe_tm()
  {
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage)) {
      TSError("FAIL:  getrusage() call failed");
      ALWAYS_ASSERT(false)
    }

    _user_tm = usage.ru_utime;
    _sys_tm  = usage.ru_stime;
  }

  void
  done()
  {
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage)) {
      TSError("FAIL:  getrusage() call failed");
      ALWAYS_ASSERT(false)
    }

    _since(_user_tm, usage.ru_utime);
    _user_tm = usage.ru_utime;

    _since(_sys_tm, usage.ru_stime);
    _sys_tm = usage.ru_stime;
  }

  unsigned long
  user_tm() const
  {
    return _usecs(_user_tm);
  }

  unsigned long
  sys_tm() const
  {
    return _usecs(_sys_tm);
  }

private:
  timeval _user_tm, _sys_tm;

  static unsigned long
  _usecs(const timeval &tv)
  {
    return (tv.tv_sec * 1000000) + tv.tv_usec;
  }

  // tm is end time on intput, time different from start to end on output.
  static void
  _since(const timeval start, timeval &tm)
  {
    if (tm.tv_sec == start.tv_sec) {
      tm.tv_sec = 0;
      tm.tv_usec -= start.tv_usec;
    } else {
      tm.tv_usec += 1000000 - start.tv_usec;
      tm.tv_sec -= start.tv_sec + 1;
      if (tm.tv_usec >= 1000000) {
        tm.tv_usec -= 1000000;
        ++tm.tv_sec;
      }
    }
  }
};

void
showPerformance(char const *tag, unsigned repetitions, const Exe_tm &tm)
{
  logFile << "Performance for " << tag << ", " << repetitions << " repetitions, microseconds user=" << tm.user_tm()
          << " system=" << tm.sys_tm() << std::endl;
}

void
testDbgOutputPerformance()
{
  static const char dbg_tag[]      = PINAME "_dbg_perf";
  static const char env_var_name[] = "TS_AU_DBG_PERF_REPS";

  static unsigned repetitions;

  if (repetitions) {
    // Already been done.
    return;
  }
  char const *env_value = std::getenv(env_var_name);

  if (!env_value || !*env_value) {
    return;
  }

  if (std::sscanf(env_value, "%u", &repetitions) != 1) {
    TSError("Environment variable %s is not a positive number", env_var_name);
    return;
  }

  for (int i = 3; i--;) {
    {
      Exe_tm tm;

      for (unsigned i = 0; i < repetitions; ++i) {
        TSDebug(dbg_tag, "Debug output test no parameters");
      }

      tm.done();

      showPerformance("TSDebug() with no parameters", repetitions, tm);
    }
    {
      Exe_tm tm;

      for (unsigned i = 0; i < repetitions; ++i) {
        TSDEBUG(dbg_tag, "Debug output test no parameters");
      }

      tm.done();

      showPerformance("TSDEBUG() with no parameters", repetitions, tm);
    }
    static int p[5];
    {
      Exe_tm tm;

      for (unsigned i = 0; i < repetitions; ++i) {
        TSDebug(dbg_tag, "Debug output test with parameters %d %d %d %d %d", p[0], p[1], p[2], p[3], p[4]);
      }

      tm.done();

      showPerformance("TSDebug() with parameters", repetitions, tm);
    }
    {
      Exe_tm tm;

      for (unsigned i = 0; i < repetitions; ++i) {
        TSDEBUG(dbg_tag, "Debug output test with parameters %d %d %d %d %d", p[0], p[1], p[2], p[3], p[4]);
      }

      tm.done();

      showPerformance("TSDEBUG() with parameters", repetitions, tm);
    }
    static volatile int pv[5];
    {
      Exe_tm tm;

      for (unsigned i = 0; i < repetitions; ++i) {
        TSDebug(dbg_tag, "Debug output test with parameters %d %d %d %d %d", pv[0], pv[1], pv[2], pv[3], pv[4]);
      }

      tm.done();

      showPerformance("TSDebug() with volatile parameters", repetitions, tm);
    }
    {
      Exe_tm tm;

      for (unsigned i = 0; i < repetitions; ++i) {
        TSDEBUG(dbg_tag, "Debug output test with parameters %d %d %d %d %d", pv[0], pv[1], pv[2], pv[3], p[4]);
      }

      tm.done();

      showPerformance("TSDEBUG() with volatile parameters", repetitions, tm);
    }
  }
}

class Cleanup
{
public:
  ~Cleanup()
  {
    // In practice it is not strictly necessary to destroy remaining continuations on program exit.

    if (tCont) {
      TSContDestroy(tCont);
    }
    if (gCont) {
      TSContDestroy(gCont);
    }
  }
};

// Do any needed cleanup for this source file at program termination time.
//
Cleanup cleanup;

} // end anonymous namespace
