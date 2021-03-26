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
#include "limiter.h"

///////////////////////////////////////////////////////////////////////////////
// This is the continuation that gets scheduled perdiocally to process the
// deque of waiting TXNs.
//
int
RateLimiter::queue_process_cont(TSCont cont, TSEvent event, void *edata)
{
  RateLimiter *limiter = static_cast<RateLimiter *>(TSContDataGet(cont));

  while (limiter->size() > 0 && limiter->reserve()) {
    QueueItem item = limiter->pop();

    TSDebug(PLUGIN_NAME, "Enabling queued txn");
    // Since this was a delayed transaction, we need to add the TXN_CLOSE hook to free the slot when done
    TSHttpTxnHookAdd(std::get<0>(item), TS_HTTP_TXN_CLOSE_HOOK, std::get<1>(item));
    TSHttpTxnReenable(std::get<0>(item), TS_EVENT_HTTP_CONTINUE);
  }

  return TS_EVENT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// The main rate limiting continuation. ToDo: Maybe this should be in the
// RateLimiter class (static)?
//
int
RateLimiter::rate_limit_cont(TSCont cont, TSEvent event, void *edata)
{
  RateLimiter *limiter = static_cast<RateLimiter *>(TSContDataGet(cont));
  TSDebug(PLUGIN_NAME, "rate_limit_cont() called with event == %d", static_cast<int>(event));

  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE:
    TSDebug(PLUGIN_NAME, "Decrementing active count");
    limiter->release();
    TSContDestroy(cont); // We are done with this continuation now
    TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
    return TS_EVENT_CONTINUE;
    break;

  case TS_EVENT_HTTP_POST_REMAP:
    TSDebug(PLUGIN_NAME, "Delaying request");
    limiter->push(static_cast<TSHttpTxn>(edata), cont);
    return TS_EVENT_NONE;
    break;

  default:
    TSDebug(PLUGIN_NAME, "Unknown event");
    TSError("Unknown event in %s", PLUGIN_NAME);
    break;
  }
  return TS_EVENT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// Setup the continuous queue processing continuation
//
void
RateLimiter::setupQueueCont()
{
  _queue_cont = TSContCreate(queue_process_cont, TSMutexCreate());
  TSReleaseAssert(_queue_cont);
  TSContDataSet(_queue_cont, this);
  _action = TSContScheduleEveryOnPool(_queue_cont, QUEUE_DELAY_TIME, TS_THREAD_POOL_TASK);
}
