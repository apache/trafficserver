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


function schedule()
    ts.debug('test schedule starts')

    -- test.com needs to be handled by ATS in remap.config
    local url = 'http://test.com/test.php'
    local res = ts.fetch(url)
    ts.debug('test fetch')
    if res.status == 200 then
        local b = res.body
        local len = string.len(b)
        ts.debug(len)
    end
    ts.debug('test schedule ends')
end

function cache_lookup()
  ts.debug('cache-lookup')
  ts.schedule(TS_LUA_THREAD_POOL_NET, 0, schedule)
  return 0
end

function do_global_read_request()
    local inner = ts.http.is_internal_request()
    if inner ~= 0 then
        ts.debug('internal')
        return 0
    end

    ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
    return 0
end
