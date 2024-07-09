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

#include <thread>
#include <chrono>
#include <atomic>

#include <ts/ts.h>
#include <tscpp/api/Cleanup.h>

using atscppapi::TSContUniqPtr;
using atscppapi::TSThreadUniqPtr;

/*
Test handling a blocking call (in a spawned thread) on a transaction hook without blocking the thread executing the hooks.

It is dependent on continuations hooked globally on a transaction hook running before continations hooked for just the
one transaction.  It is dependent on the ability of a global continuation on a txn hook to add a per-txn continuation on
the same hook.
*/

#define PINAME "polite_hook_wait"

namespace
{
char PIName[] = PINAME;

DbgCtl dbg_ctl{PINAME};

enum Test_step { BEGIN, GLOBAL_CONT_READ_HDRS, THREAD, TXN_CONT_READ_HDRS, END };

char const *
step_cstr(int test_step)
{
  char const *result{"BAD TEST STEP"};

  switch (test_step) {
  case BEGIN:
    result = "BEGIN";
    break;

  case GLOBAL_CONT_READ_HDRS:
    result = "GLOBAL_CONT_READ_HDRS";
    break;

  case THREAD:
    result = "THREAD";
    break;

  case TXN_CONT_READ_HDRS:
    result = "TXN_CONT_READ_HDRS";
    break;

  default:
    break;
  }

  return result;
}

int txn_count{0};

void
next_step(int curr)
{
  static std::atomic<int> test_step{BEGIN};

  TSReleaseAssert(test_step.load(std::memory_order_relaxed) == curr);

  if (BEGIN == curr) {
    ++txn_count;

    TSReleaseAssert(txn_count <= 2);
  }

  ++curr;
  if (END == curr) {
    curr = BEGIN;
  }

  Dbg(dbg_ctl, "Entering test step %s", step_cstr(curr));

  test_step.store(curr, std::memory_order_relaxed);
}

atscppapi::TxnAuxMgrData mgr_data;

class Blocking_action
{
public:
  static void init();

private:
  ~Blocking_action()
  {
    // This should either not block, or only block very briefly.
    //
    TSThreadWait(_checker.get());

    Dbg(dbg_ctl, "In ~Blocking_action()");
  }

  Blocking_action() = default;

  static int   _global_cont_func(TSCont, TSEvent event, void *eventData);
  static int   _txn_cont_func(TSCont, TSEvent event, void *eventData);
  static void *_thread_func(void *vba);

  TSContUniqPtr     _txn_hook_cont{TSContCreate(_txn_cont_func, TSMutexCreate())};
  std::atomic<bool> _cont_mutex_locked{false};

  TSThreadUniqPtr _checker{TSThreadCreate(&_thread_func, this)};

  bool txn_valid{false};

  friend class atscppapi::TxnAuxDataMgr<Blocking_action, mgr_data>;
};

using AuxDataMgr = atscppapi::TxnAuxDataMgr<Blocking_action, mgr_data>;

void
Blocking_action::init()
{
  static TSContUniqPtr global{TSContCreate(_global_cont_func, nullptr)};

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, global.get());
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, global.get());
}

int
Blocking_action::_global_cont_func(TSCont, TSEvent event, void *eventData)
{
  Dbg(dbg_ctl, "entering _global_cont_func()");

  TSReleaseAssert(eventData != nullptr);

  TSHttpTxn txn{static_cast<TSHttpTxn>(eventData)};

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    next_step(BEGIN);

    Blocking_action &ba = AuxDataMgr::data(txn);

    if (!ba._checker.get()) {
      TSError(PINAME ": failed to create thread");
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }

    TSHttpTxnHookAdd(txn, TS_HTTP_READ_REQUEST_HDR_HOOK, ba._txn_hook_cont.get());

    while (!ba._cont_mutex_locked.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  } break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    next_step(TXN_CONT_READ_HDRS);

    if (!AuxDataMgr::data(txn).txn_valid) {
      static const char msg[] = "authorization denied\n";

      TSHttpTxnErrorBodySet(txn, TSstrdup(msg), sizeof(msg) - 1, TSstrdup("text/plain"));
    }

    break;

  default:
    TSReleaseAssert(false);
    break;
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

void *
Blocking_action::_thread_func(void *vba)
{
  next_step(GLOBAL_CONT_READ_HDRS);

  auto ba = static_cast<Blocking_action *>(vba);

  TSMutexLock(TSContMutexGet(ba->_txn_hook_cont.get())); // This will never block.
  ba->_cont_mutex_locked.store(true, std::memory_order_release);

  // This is a stand-in for some blocking call to validate the HTTP request in some way.
  //
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Pass "validation" for first transaction, fail it for second.
  //
  if (1 == txn_count) {
    ba->txn_valid = true;
  }

  // Let per-txn continuation run.
  //
  TSMutexUnlock(TSContMutexGet(ba->_txn_hook_cont.get()));

  return nullptr;
}

int
Blocking_action::_txn_cont_func(TSCont, TSEvent event, void *eventData)
{
  next_step(THREAD);

  TSReleaseAssert(eventData != nullptr);
  TSReleaseAssert(TS_EVENT_HTTP_READ_REQUEST_HDR == event);

  TSHttpTxn txn{static_cast<TSHttpTxn>(eventData)};

  Blocking_action &ba = AuxDataMgr::data(txn);

  if (!ba.txn_valid) {
    TSHttpTxnStatusSet(txn, TS_HTTP_STATUS_FORBIDDEN);
  }

  TSHttpTxnReenable(txn, ba.txn_valid ? TS_EVENT_HTTP_CONTINUE : TS_EVENT_HTTP_ERROR);

  return 0;
}

} // end anonymous namespace

void
TSPluginInit(int /* n_arg ATS_UNUSED */, char const ** /* arg ATS_UNUSED */)
{
  Dbg(dbg_ctl, "initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PIName);
  info.vendor_name   = const_cast<char *>("apache");
  info.support_email = const_cast<char *>("edge@yahooinc.com");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PINAME ": failure calling TSPluginRegister.");
    return;
  } else {
    Dbg(dbg_ctl, "Plugin registration succeeded.");
  }

  AuxDataMgr::init(PIName);

  Blocking_action::init();
}
