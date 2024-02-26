/*
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

#include <fmt/core.h>

#include "ts/ts.h"

#include "cripts/Error.hpp"

// This had to be extraced out of the CriptContex class, to avoid the circular references. And
// doing this lets us have the various headers, URLs etc. as uninitialized members of the Context.

namespace Cript
{
// This is a bitfield, used to disable a particular callback from a previous hook
enum Callbacks {
  NONE             = 0,
  DO_REMAP         = 1,
  DO_POST_REMAP    = 2,
  DO_SEND_RESPONSE = 4,
  DO_CACHE_LOOKUP  = 8,
  DO_SEND_REQUEST  = 16,
  DO_READ_RESPONSE = 32,
  DO_TXN_CLOSE     = 64
};

class Transaction
{
public:
  void
  disableCallback(Callbacks cb)
  {
    enabled_hooks &= ~cb;
  }

  // This is crucial, we have to get the Txn pointer early on and preserve it.
  TSHttpTxn txnp;
  TSHttpSsn ssnp;
  Error error;
  Context *context; // Back to the owning context

  // Keep track of which hook we're currently in. ToDo: Do we still need this with the
  // tests being moved out to the linter?
  TSHttpHookID hook      = TS_HTTP_LAST_HOOK;
  unsigned enabled_hooks = 0; // Which hooks are enabled, other than the mandatory ones

  [[nodiscard]] bool
  aborted() const
  {
    bool client_abort = false;

    return (TSHttpTxnAborted(txnp, &client_abort) == TS_SUCCESS);
  }

  [[nodiscard]] int
  lookupStatus() const
  {
    int status = 0;

    if (TSHttpTxnCacheLookupStatusGet(txnp, &status) != TS_SUCCESS) {
      return -1;
    }

    return status;
  }

}; // class Transaction

} // namespace Cript
