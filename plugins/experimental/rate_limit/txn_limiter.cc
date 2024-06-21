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
#include <unistd.h>
#include <getopt.h>
#include <cstdlib>

#include "txn_limiter.h"

///////////////////////////////////////////////////////////////////////////////
// These continuations are "helpers" to the TXN limiter object. Putting them
// outside the class implementation is just cleaner.
//
static int
txn_limit_cont(TSCont cont, TSEvent event, void *edata)
{
  auto *limiter = static_cast<TxnRateLimiter *>(TSContDataGet(cont));

  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE:
    limiter->free();
    TSContDestroy(cont); // We are done with this continuation now
    TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
    return TS_EVENT_CONTINUE;
    break;

  case TS_EVENT_HTTP_SSN_CLOSE:
    limiter->free();
    TSContDestroy(cont); // We are done with this continuation now
    TSHttpSsnReenable(static_cast<TSHttpSsn>(edata), TS_EVENT_HTTP_CONTINUE);
    return TS_EVENT_NONE;
    break;

  case TS_EVENT_HTTP_POST_REMAP:
    limiter->push(static_cast<TSHttpTxn>(edata), cont);
    limiter->incrementMetric(RATE_LIMITER_METRIC_QUEUED);
    return TS_EVENT_NONE;
    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: // This is only applicable when we set an error in remap
    retryAfter(static_cast<TSHttpTxn>(edata), limiter->retry());
    TSContDestroy(cont); // We are done with this continuation now
    TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
    limiter->incrementMetric(RATE_LIMITER_METRIC_REJECTED);
    return TS_EVENT_CONTINUE;
    break;

  default:
    Dbg(dbg_ctl, "Unknown event %d", static_cast<int>(event));
    TSError("Unknown event in %s", PLUGIN_NAME);
    break;
  }
  return TS_EVENT_NONE;
}

static int
txn_queue_cont(TSCont cont, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  auto     *limiter = static_cast<TxnRateLimiter *>(TSContDataGet(cont));
  QueueTime now     = std::chrono::system_clock::now(); // Only do this once per "loop"

  // Try to enable some queued txns (if any) if there are slots available
  while (limiter->size() > 0 && limiter->reserve() != ReserveStatus::FULL) { // Can't be UNLIMITED here
    auto [txnp, contp, start_time]  = limiter->pop();
    std::chrono::milliseconds delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

    delayHeader(txnp, limiter->header(), delay);
    Dbg(dbg_ctl, "Enabling queued txn after %ldms", static_cast<long>(delay.count()));
    // Since this was a delayed transaction, we need to add the TXN_CLOSE hook to free the slot when done
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, contp);
    limiter->incrementMetric(RATE_LIMITER_METRIC_RESUMED);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }

  // Kill any queued txns if they are too old
  if (limiter->size() > 0 && limiter->max_age() > std::chrono::milliseconds::zero()) {
    now = std::chrono::system_clock::now(); // Update the "now", for some extra accuracy

    while (limiter->size() > 0 && limiter->hasOldEntity(now)) {
      // The oldest object on the queue is too old on the queue, so "kill" it.
      auto [txnp, contp, start_time] = limiter->pop();
      std::chrono::milliseconds age  = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

      delayHeader(txnp, limiter->header(), age);
      Dbg(dbg_ctl, "Queued TXN is too old (%ldms), erroring out", static_cast<long>(age.count()));
      TSHttpTxnStatusSet(txnp, static_cast<TSHttpStatus>(limiter->error()));
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      limiter->incrementMetric(RATE_LIMITER_METRIC_EXPIRED);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    }
  }

  return TS_EVENT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// Parse the configurations for the TXN limiter.
//
bool
TxnRateLimiter::initialize(int argc, const char *argv[])
{
  static const struct option longopt[] = {
    {const_cast<char *>("limit"),     required_argument, nullptr, 'l' },
    {const_cast<char *>("queue"),     required_argument, nullptr, 'q' },
    {const_cast<char *>("error"),     required_argument, nullptr, 'e' },
    {const_cast<char *>("retry"),     required_argument, nullptr, 'r' },
    {const_cast<char *>("header"),    required_argument, nullptr, 'h' },
    {const_cast<char *>("maxage"),    required_argument, nullptr, 'm' },
    {const_cast<char *>("prefix"),    required_argument, nullptr, 'p' },
    {const_cast<char *>("tag"),       required_argument, nullptr, 't' },
    {const_cast<char *>("conntrack"), no_argument,       nullptr, 'c' },
    {const_cast<char *>("rate"),      required_argument, nullptr, 'R' },
    // EOF
    {nullptr,                         no_argument,       nullptr, '\0'},
  };
  optind             = 1;
  std::string prefix = RATE_LIMITER_METRIC_PREFIX;
  std::string tag    = "";

  while (true) {
    int opt = getopt_long(argc, const_cast<char *const *>(argv), "", longopt, nullptr);

    switch (opt) {
    case 'l':
      this->_limit = strtol(optarg, nullptr, 10);
      break;
    case 'q':
      this->_max_queue = strtol(optarg, nullptr, 10);
      break;
    case 'e':
      this->_error = strtol(optarg, nullptr, 10);
      break;
    case 'r':
      this->_retry = strtol(optarg, nullptr, 10);
      break;
    case 'm':
      this->_max_age = std::chrono::milliseconds(strtol(optarg, nullptr, 10));
      break;
    case 'h':
      this->_header = optarg;
      break;
    case 'p':
      prefix = optarg;
      break;
    case 't':
      tag = optarg;
      break;
    case 'c':
      this->_conntrack = true;
      break;
    case 'R':
      this->_rate = strtol(optarg, nullptr, 10);
      break;
    }
    if (opt == -1) {
      break;
    }
  }

  if (this->max_queue() > 0 && this->has_limit()) { // Only setup the queue if we have a limit
    _queue_cont = TSContCreate(txn_queue_cont, TSMutexCreate());
    TSReleaseAssert(_queue_cont);
    TSContDataSet(_queue_cont, this);
    _action = TSContScheduleEveryOnPool(_queue_cont, QUEUE_DELAY_TIME.count(), TS_THREAD_POOL_TASK);
  }

  this->initializeMetrics(RATE_LIMITER_TYPE_REMAP, tag, prefix);

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Sets up a transaction based continuation for this transaction.
//
void
TxnRateLimiter::setupTxnCont(TSHttpTxn txnp, TSHttpHookID hook)
{
  TSCont cont = TSContCreate(txn_limit_cont, nullptr);
  TSReleaseAssert(cont);

  TSContDataSet(cont, this);
  TSHttpTxnHookAdd(txnp, hook, cont);
}

// This only needs the TS_HTTP_SSN_CLOSE_HOOK, for now at least, so not passed as argument.
void
TxnRateLimiter::setupSsnCont(TSHttpSsn ssnp)
{
  TSCont cont = TSContCreate(txn_limit_cont, nullptr);
  TSReleaseAssert(cont);

  TSContDataSet(cont, this);
  TSHttpSsnHookAdd(ssnp, TS_HTTP_SSN_CLOSE_HOOK, cont);
}
