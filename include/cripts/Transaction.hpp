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

namespace cripts
{
// This is a bitfield, used to disable a particular callback from a previous hook
enum Callbacks : std::uint32_t {
  NONE              = 0,
  DO_REMAP          = 1 << 0,
  DO_POST_REMAP     = 1 << 1,
  DO_CACHE_LOOKUP   = 1 << 2,
  DO_SEND_REQUEST   = 1 << 3,
  DO_READ_RESPONSE  = 1 << 4,
  DO_SEND_RESPONSE  = 1 << 5,
  DO_TXN_CLOSE      = 1 << 6,
  GLB_TXN_START     = 1 << 7,
  GLB_READ_REQUEST  = 1 << 8,
  GLB_PRE_REMAP     = 1 << 9,
  GLB_POST_REMAP    = 1 << 10,
  GLB_CACHE_LOOKUP  = 1 << 11,
  GLB_SEND_REQUEST  = 1 << 12,
  GLB_READ_RESPONSE = 1 << 13,
  GLB_SEND_RESPONSE = 1 << 14,
  GLB_TXN_CLOSE     = 1 << 15,
};

class Transaction
{
public:
  void
  DisableCallback(Callbacks cb)
  {
    enabled_hooks &= ~cb;
  }

  // This is crucial, we have to get the Txn pointer early on and preserve it.
  TSHttpTxn txnp;
  TSHttpSsn ssnp;
  Error     error;
  Context  *context; // Back to the owning context

  // Keep track of which hook we're currently in. ToDo: Do we still need this with the
  // tests being moved out to the linter?
  TSHttpHookID hook          = TS_HTTP_LAST_HOOK;
  uint32_t     enabled_hooks = 0; // Which hooks are enabled, other than the mandatory ones

  [[nodiscard]] bool
  Aborted() const
  {
    bool client_abort = false;

    if (TSHttpTxnAborted(txnp, &client_abort) == TS_SUCCESS) {
      return client_abort;
    }

    return false;
  }

  [[nodiscard]] int
  LookupStatus() const
  {
    int status = 0;

    if (TSHttpTxnCacheLookupStatusGet(txnp, &status) != TS_SUCCESS) {
      return -1;
    }

    return status;
  }

}; // class Transaction

} // namespace cripts
