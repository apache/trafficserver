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
Regression testing code for CPP API.  Not comprehensive, hopefully will be built up over time.
*/

#include <vector>
#include <utility>
#include <sstream>
#include <fstream>

#include <tscpp/util/TextView.h>

#include <tscpp/api/Continuation.h>
#include <tscpp/api/GlobalPlugin.h>
#include <tscpp/api/SessionPlugin.h>
#include <tscpp/api/Session.h>
#include <tscpp/api/TransactionPlugin.h>
#include <tscpp/api/Transaction.h>

#include <ts/ts.h>

// TSReleaseAssert() doesn't seem to produce any logging output for a debug build, so do both kinds of assert.
//
#define ALWAYS_ASSERT(EXPR) \
  {                         \
    bool val = (EXPR);      \
    TSAssert(val);          \
    TSReleaseAssert(val);   \
  }

namespace
{
#define PINAME "test_cppapi"

// Au tests should ensure there is only one virtual connection (with one associated session and transation) at a time.
// That along with hook serialization should ensure mutual explusion of the logFile object.
//
std::fstream logFile;

std::vector<void (*)()> testList;

struct ATest {
  ATest(void (*testFuncPtr)()) { testList.push_back(testFuncPtr); }
};

// Put a test function, whose name is the actual parameter for TEST_FUNC, into testList, the list of test functions.
//
#define TEST(TEST_FUNC) ATest t(TEST_FUNC);

} // end anonymous namespace

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

// Test for Continuation class.
//
namespace PluginTest
{
// These are to test that the exported names are in th atscppapi namespace.
//
using GlobalPlugin_      = atscppapi::GlobalPlugin;
using SessionPlugin_     = atscppapi::SessionPlugin;
using Session_           = atscppapi::Session;
using TransactionPlugin_ = atscppapi::TransactionPlugin;
using Transaction_       = atscppapi::Transaction;

const Session_ *currentSession;

void
checkSession(const Session_ &s)
{
  if (!currentSession) {
    currentSession = &s;
  } else {
    ALWAYS_ASSERT(&s == currentSession);
  }

  ALWAYS_ASSERT(!s.isInternalRequest());

  ALWAYS_ASSERT(TSHttpSsnIncomingAddrGet(static_cast<TSHttpSsn>(s.getAtsHandle())) == s.getIncomingAddress());
  ALWAYS_ASSERT(TSHttpSsnClientAddrGet(static_cast<TSHttpSsn>(s.getAtsHandle())) == s.getClientAddress());
}

const Transaction_ *currentTransaction;

void
checkTransaction(const Transaction_ &t, bool hasSessionObj = true)
{
  if (!currentTransaction) {
    currentTransaction = &t;
  } else {
    ALWAYS_ASSERT(&t == currentTransaction);
  }

  ALWAYS_ASSERT(!t.isInternalRequest());

  ALWAYS_ASSERT(t.sessionObjExists() == hasSessionObj);

  if (hasSessionObj) {
    checkSession(t.session());
  } else {
    ALWAYS_ASSERT(!currentSession);
  }

  ALWAYS_ASSERT(TSHttpTxnIncomingAddrGet(static_cast<TSHttpTxn>(t.getAtsHandle())) == t.getIncomingAddress());
  ALWAYS_ASSERT(TSHttpTxnClientAddrGet(static_cast<TSHttpTxn>(t.getAtsHandle())) == t.getClientAddress());
  ALWAYS_ASSERT(TSHttpTxnNextHopAddrGet(static_cast<TSHttpTxn>(t.getAtsHandle())) == t.getNextHopAddress());
}

#define LTH(X)                                                               \
  X(HOOK_READ_REQUEST_HEADERS_PRE_REMAP, handleReadRequestHeadersPreRemap)   \
  X(HOOK_READ_REQUEST_HEADERS_POST_REMAP, handleReadRequestHeadersPostRemap) \
  X(HOOK_SEND_REQUEST_HEADERS, handleSendRequestHeaders)                     \
  X(HOOK_READ_REQUEST_HEADERS, handleReadRequestHeaders)                     \
  X(HOOK_READ_RESPONSE_HEADERS, handleReadResponseHeaders)                   \
  X(HOOK_SEND_RESPONSE_HEADERS, handleSendResponseHeaders)                   \
  X(HOOK_OS_DNS, handleOsDns)                                                \
  X(HOOK_CACHE_LOOKUP_COMPLETE, handleReadCacheLookupComplete)

#define X(HOOK, FUNC)                                                                                                 \
  class TestTransactionPlugin_##HOOK : public TransactionPlugin_                                                      \
  {                                                                                                                   \
  public:                                                                                                             \
    TestTransactionPlugin_##HOOK(Transaction_ &transaction) : TransactionPlugin_(transaction) { registerHook(HOOK); } \
    void                                                                                                              \
    FUNC(Transaction_ &transaction) override                                                                          \
    {                                                                                                                 \
      logFile << "TestTransactionPlugin2_" #HOOK "::" #FUNC "()\n";                                                   \
      checkTransaction(transaction);                                                                                  \
      transaction.resume();                                                                                           \
    }                                                                                                                 \
    ~TestTransactionPlugin_##HOOK()                                                                                   \
    {                                                                                                                 \
      ALWAYS_ASSERT(transactionObjExists());                                                                          \
      checkTransaction(getTransaction());                                                                             \
    }                                                                                                                 \
  };

LTH(X)

#undef X

class TestSessionPlugin : public SessionPlugin_
{
public:
  TestSessionPlugin(Session_ &session) : SessionPlugin_(session)
  {
    ++_instanceCount;

    registerHook(HOOK_TXN_START);
  }

  void
  handleTransactionStart(Transaction_ &transaction) override
  {
    logFile << "TestSessionPlugin::handleTransactionStart()\n";

    checkTransaction(transaction);

#define X(HOOK, FUNC) transaction.addPlugin(new TestTransactionPlugin_##HOOK(transaction));

    LTH(X)

#undef X

    transaction.resume();
  }

  ~TestSessionPlugin()
  {
    --_instanceCount;

    ALWAYS_ASSERT(sessionObjExists());

    checkSession(getSession());

    if (0 == _instanceCount) {
      currentSession     = nullptr;
      currentTransaction = nullptr;
    }
  }

private:
  static unsigned _instanceCount;
};

unsigned TestSessionPlugin::_instanceCount;

#define X(HOOK, FUNC)                                                                              \
  class TestSessionPlugin2_##HOOK : public SessionPlugin_                                          \
  {                                                                                                \
  public:                                                                                          \
    TestSessionPlugin2_##HOOK(Session_ &session) : SessionPlugin_(session) { registerHook(HOOK); } \
    void                                                                                           \
    FUNC(Transaction_ &transaction) override                                                       \
    {                                                                                              \
      logFile << "TestSessionPlugin2_" #HOOK "::" #FUNC "()\n";                                    \
      checkTransaction(transaction);                                                               \
      transaction.resume();                                                                        \
    }                                                                                              \
  };

LTH(X)

#undef X

class TestGlobalPlugin : public GlobalPlugin_
{
public:
  void
  handleSessionStart(Session_ &session) override
  {
    logFile << "TestGlobalPlugin::handleSessionStart()\n";

    checkSession(session);

    session.addPlugin(new TestSessionPlugin(session));

#define X(HOOK, FUNC) session.addPlugin(new TestSessionPlugin2_##HOOK(session));

    LTH(X)

#undef X

    session.resume();
  }

  ~TestGlobalPlugin()
  {
    ALWAYS_ASSERT(!currentTransaction);
    ALWAYS_ASSERT(!currentSession);
  }
};

TestGlobalPlugin testGlobalPlugin;

#define X(HOOK, FUNC)                                          \
  class TestGlobalPlugin2_##HOOK : public GlobalPlugin_        \
  {                                                            \
  public:                                                      \
    void                                                       \
    FUNC(Transaction_ &transaction) override                   \
    {                                                          \
      logFile << "TestGlobalPlugin2_" #HOOK "::" #FUNC "()\n"; \
      checkTransaction(transaction);                           \
      transaction.resume();                                    \
    }                                                          \
  };                                                           \
  TestGlobalPlugin2_##HOOK testGlobalPlugin2_##HOOK;

LTH(X)

#undef X

class TestGlobalPlugin3 : public GlobalPlugin_
{
public:
  void
  handleTransactionStart(Transaction_ &transaction) override
  {
    logFile << "TestGlobalPlugin3::handleTransactionStart()\n";

    checkTransaction(transaction);

    transaction.resume();
  }
};

TestGlobalPlugin3 testGlobalPlugin3;

void
f()
{
  testGlobalPlugin.registerHook(GlobalPlugin_::HOOK_SSN_START);
  testGlobalPlugin3.registerHook(SessionPlugin_::HOOK_TXN_START);

#define X(HOOK, FUNC) testGlobalPlugin2_##HOOK.registerHook(TransactionPlugin_::HOOK);

  LTH(X)

#undef X

#define X(HOOK, FUNC) ALWAYS_ASSERT(TransactionPlugin_::HOOK_TYPE_STRINGS[TransactionPlugin_::HOOK] == #HOOK);

  LTH(X)

#undef X

  ALWAYS_ASSERT(SessionPlugin_::HOOK_TYPE_STRINGS[SessionPlugin_::HOOK_TXN_START] == "HOOK_TXN_START");

  ALWAYS_ASSERT(GlobalPlugin_::HOOK_TYPE_STRINGS[GlobalPlugin_::HOOK_SSN_START] == "HOOK_SSN_START");
  ALWAYS_ASSERT(GlobalPlugin_::HOOK_TYPE_STRINGS[GlobalPlugin_::HOOK_SELECT_ALT] == "HOOK_SELECT_ALT");
}

#undef LTH

TEST(f)

} // end namespace PluginTest

// Run all the tests.
//
void
TSPluginInit(int, const char **)
{
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

  ALWAYS_ASSERT(atscppapi::RegisterGlobalPlugin("test_cppapi", "Apache Software Foundation", "dev@trafficserver.apache.org"));

  for (auto fp : testList) {
    fp();
  }
}
