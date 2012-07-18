--  Licensed to the Apache Software Foundation (ASF) under one
--  or more contributor license agreements.  See the NOTICE file
--  distributed with this work for additional information
--  regarding copyright ownership.  The ASF licenses this file
--  to you under the Apache License, Version 2.0 (the
--  "License"); you may not use this file except in compliance
--  with the License.  You may obtain a copy of the License at
--
--  http://www.apache.org/licenses/LICENSE-2.0
--
--  Unless required by applicable law or agreed to in writing, software
--  distributed under the License is distributed on an "AS IS" BASIS,
--  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
--  See the License for the specific language governing permissions and
--  limitations under the License.

-- Example Lua hooks plugin to demonstrate the use of global, per-session and per-transaction hooks.

require 'string'
require 'debug'

ts = require 'ts'
ts.hook = require 'ts.hook'

function OnSession(event, ssn)
    -- NOTE: the 'ssn' argument is either a session or a transaction object, depending on the event.
    ts.debug('hooks', string.format('handling session event %d', event))
    ssn:continue()
end

-- Callback function for per-transaction hooks.
function OnTransaction(event, txn)
    ts.debug('hooks', string.format('handling transaction event %d', event))
   txn:continue()
end

events = {
    ssn = {},
    txn = {}
}

events.txn[ts.hook.HTTP_OS_DNS_HOOK]                = OnTransaction
events.txn[ts.hook.HTTP_READ_CACHE_HDR_HOOK]        = OnTransaction
events.txn[ts.hook.HTTP_READ_REQUEST_HDR_HOOK]      = OnTransaction
events.txn[ts.hook.HTTP_SEND_REQUEST_HDR_HOOK]      = OnTransaction
events.txn[ts.hook.HTTP_READ_RESPONSE_HDR_HOOK]     = OnTransaction
events.txn[ts.hook.HTTP_SEND_RESPONSE_HDR_HOOK]     = OnTransaction
events.txn[ts.hook.HTTP_TXN_START_HOOK]             = OnTransaction
events.txn[ts.hook.HTTP_TXN_CLOSE_HOOK]             = OnTransaction
events.txn[ts.hook.HTTP_SSN_START_HOOK]             = OnTransaction
events.txn[ts.hook.HTTP_SSN_CLOSE_HOOK]             = OnTransaction
events.txn[ts.hook.HTTP_CACHE_LOOKUP_COMPLETE_HOOK] = OnTransaction
events.txn[ts.hook.HTTP_PRE_REMAP_HOOK]             = OnTransaction
events.txn[ts.hook.HTTP_POST_REMAP_HOOK]            = OnTransaction

events.ssn[ts.hook.HTTP_READ_REQUEST_HDR_HOOK]      = OnSession
events.ssn[ts.hook.HTTP_OS_DNS_HOOK]                = OnSession
events.ssn[ts.hook.HTTP_SEND_REQUEST_HDR_HOOK]      = OnSession
events.ssn[ts.hook.HTTP_READ_CACHE_HDR_HOOK]        = OnSession
events.ssn[ts.hook.HTTP_READ_RESPONSE_HDR_HOOK]     = OnSession
events.ssn[ts.hook.HTTP_SEND_RESPONSE_HDR_HOOK]     = OnSession
events.ssn[ts.hook.HTTP_TXN_START_HOOK]             = OnSession
events.ssn[ts.hook.HTTP_TXN_CLOSE_HOOK]             = OnSession
events.ssn[ts.hook.HTTP_SSN_START_HOOK]             = OnSession
events.ssn[ts.hook.HTTP_SSN_CLOSE_HOOK]             = OnSession
events.ssn[ts.hook.HTTP_CACHE_LOOKUP_COMPLETE_HOOK] = OnSession
events.ssn[ts.hook.HTTP_PRE_REMAP_HOOK]             = OnSession
events.ssn[ts.hook.HTTP_POST_REMAP_HOOK]            = OnSession

ts.debug('hooks', string.format('loaded %s', debug.getinfo(1).source))

-- Hook the global session start so we can register the per-session events.
ts.hook.register(ts.hook.HTTP_SSN_START_HOOK,
    function(event, ssn)
        ts.debug('hooks', string.format('callback for HTTP_SSN_START event=%d', event))
        ssn:register(events.ssn)
        ssn:continue()
    end
)

-- Hook the global transaction start so we can register the per-transaction events.
ts.hook.register(ts.hook.HTTP_TXN_START_HOOK,
    function(event, txn)
        ts.debug('hooks', string.format('callback for HTTP_TXN_START event=%d', event))
        txn:register(events.txn)
        txn:continue()
    end
)

-- vim: set sw=4 ts=4 et :
