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

-- Lua example to log the HTTP transaction cache lookup status.

require 'string'
require 'debug'

ts = require 'ts'
ts.hook = require 'ts.hook'

ts.debug('cachestatus', string.format('loaded %s', debug.getinfo(1).source))

do

local strings = { }
strings[ts.CACHE_LOOKUP_MISS]       = "TS_CACHE_LOOKUP_MISS"
strings[ts.CACHE_LOOKUP_HIT_STALE]  = "TS_CACHE_LOOKUP_HIT_STALE"
strings[ts.CACHE_LOOKUP_HIT_FRESH]  = "TS_CACHE_LOOKUP_HIT_FRESH"
strings[ts.CACHE_LOOKUP_SKIPPED]    = "TS_CACHE_LOOKUP_SKIPPED"

function cachestatus(status)
    return strings[status]
end
end

ts.hook.register(ts.hook.HTTP_CACHE_LOOKUP_COMPLETE_HOOK,
    function(event, txn)
        ts.debug('cachestatus',
            string.format('cache lookup status is %s', cachestatus(txn:cachestatus())))
        txn:continue()
    end
)

-- vim: set sw=4 ts=4 et :
