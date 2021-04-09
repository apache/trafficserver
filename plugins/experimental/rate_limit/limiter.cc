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
// This is the continuation that gets scheduled periodically to process the
// deque of waiting TXNs.
//
int
RateLimiter::queue_process_cont(TSCont cont, TSEvent event, void *edata)
{
  RateLimiter *limiter = static_cast<RateLimiter *>(TSContDataGet(cont));
  QueueTime now        = std::chrono::system_clock::now(); // Only do this once per "loop"

  // Try to enable some queued txns (if any) if there are slots available
  while (limiter->size() > 0 && limiter->reserve()) {
    auto [txnp, contp, start_time]  = limiter->pop();
    std::chrono::microseconds delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

    limiter->delayHeader(txnp, delay);
    TSDebug(PLUGIN_NAME, "Enabling queued txn after %ldms", static_cast<long>(delay.count()));
    // Since this was a delayed transaction, we need to add the TXN_CLOSE hook to free the slot when done
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, contp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }

  // Kill any queued txns if they are too old
  if (limiter->max_age > std::chrono::milliseconds::zero() && limiter->size() > 0) {
    now = std::chrono::system_clock::now(); // Update the "now", for some extra accuracy

    while (limiter->size() > 0 && limiter->hasOldTxn(now)) {
      // The oldest object on the queue is too old on the queue, so "kill" it.
      auto [txnp, contp, start_time] = limiter->pop();
      std::chrono::milliseconds age  = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

      limiter->delayHeader(txnp, age);
      TSDebug(PLUGIN_NAME, "Queued TXN is too old (%ldms), erroring out", static_cast<long>(age.count()));
      TSHttpTxnStatusSet(txnp, static_cast<TSHttpStatus>(limiter->error));
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    }
  }

  return TS_EVENT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// The main rate limiting continuation.
//
int
RateLimiter::rate_limit_cont(TSCont cont, TSEvent event, void *edata)
{
  RateLimiter *limiter = static_cast<RateLimiter *>(TSContDataGet(cont));

  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE:
    limiter->release();
    TSContDestroy(cont); // We are done with this continuation now
    TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
    return TS_EVENT_CONTINUE;
    break;

  case TS_EVENT_HTTP_POST_REMAP:
    limiter->push(static_cast<TSHttpTxn>(edata), cont);
    return TS_EVENT_NONE;
    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: // This is only applicable when we set an error in remap
    limiter->retryAfter(static_cast<TSHttpTxn>(edata), limiter->retry);
    TSContDestroy(cont); // We are done with this continuation now
    TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
    return TS_EVENT_CONTINUE;
    break;

  default:
    TSDebug(PLUGIN_NAME, "Unknown event %d", static_cast<int>(event));
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
  _action = TSContScheduleEveryOnPool(_queue_cont, QUEUE_DELAY_TIME.count(), TS_THREAD_POOL_TASK);
}

///////////////////////////////////////////////////////////////////////////////
// Add a header with the delay imposed on this transaction. This can be used
// for logging, and other types of metrics.
//
void
RateLimiter::delayHeader(TSHttpTxn txnp, std::chrono::microseconds delay) const
{
  if (header.size() > 0) {
    TSMLoc hdr_loc   = nullptr;
    TSMBuffer bufp   = nullptr;
    TSMLoc field_loc = nullptr;

    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, header.c_str(), header.size(), &field_loc)) {
        if (TS_SUCCESS == TSMimeHdrFieldValueIntSet(bufp, hdr_loc, field_loc, -1, static_cast<int>(delay.count()))) {
          TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
        }
        TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Add a header with the delay imposed on this transaction. This can be used
// for logging, and other types of metrics.
//
void
RateLimiter::retryAfter(TSHttpTxn txnp, unsigned retry) const
{
  if (retry > 0) {
    TSMLoc hdr_loc   = nullptr;
    TSMBuffer bufp   = nullptr;
    TSMLoc field_loc = nullptr;

    if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, "Retry-After", 11, &field_loc)) {
        if (TS_SUCCESS == TSMimeHdrFieldValueIntSet(bufp, hdr_loc, field_loc, -1, retry)) {
          TSDebug(PLUGIN_NAME, "Added a Retry-After: %u", retry);
          TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
        }
        TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
  }
}
