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
#pragma once

#include "limiter.h"
#include "ts/ts.h"

///////////////////////////////////////////////////////////////////////////////
// TXN based limiters, for remap.config plugin instances.
//
class TxnRateLimiter : public RateLimiter<TSHttpTxn>
{
public:
  ~TxnRateLimiter()
  {
    if (_action) {
      TSActionCancel(_action);
    }
    if (_queue_cont) {
      TSContDestroy(_queue_cont);
    }
  }

  void setupTxnCont(TSHttpTxn txnp, TSHttpHookID hook);
  bool initialize(int argc, const char *argv[]);

  std::string header = "";  // Header to put the latency metrics in, e.g. @RateLimit-Delay
  unsigned error     = 429; // Error code when we decide not to allow a txn to be processed (e.g. queue full)
  unsigned retry     = 0;   // If > 0, we will also send a Retry-After: header with this retry value

private:
  TSCont _queue_cont = nullptr; // Continuation processing the queue periodically
  TSAction _action   = nullptr; // The action associated with the queue continuation, needed to shut it down
};
