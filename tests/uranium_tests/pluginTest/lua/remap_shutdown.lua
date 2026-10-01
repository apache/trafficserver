--  Licensed to the Apache Software Foundation (ASF) under one
--  or more contributor license agreements.  See the NOTICE file
--  distributed with this work for additional information
--  regarding copyright ownership.  The ASF licenses this file
--  to you under the Apache License, Version 2.0 (the
--  "License"); you may not use this file except in compliance
--  with the License.  You may obtain a copy of the License at
--
--      http://www.apache.org/licenses/LICENSE-2.0
--
--  Unless required by applicable law or agreed to in writing, software
--  distributed under the License is distributed on an "AS IS" BASIS,
--  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
--  See the License for the specific language governing permissions and
--  limitations under the License.

-- The remap half of the shutdown race. A global script's __shutdown__ releases
-- process global resources, so remap Lua must not run after it either. These
-- states live in the same plugin image as the global ones only when
-- proxy.config.plugin.dynamic_reload_mode is disabled.
--
-- The remap instance is configured with a single Lua state, so the first
-- /remap-hold request occupies it and every later one waits on its mutex. Those
-- waiting requests are the ones that must not enter Lua once __shutdown__ has run.
local test_directory = os.getenv('TS_LUA_SHUTDOWN_TEST_DIR')

-- Written by global_shutdown.lua once the __shutdown__ functions have run.
local shutdown_path = nil

-- Written while this state is occupied by the request below.
local active_path = nil

-- Only the first /remap-hold occupies the state; the rest queue behind it and
-- return promptly, so the load keeps requests waiting on the mutex.
local held_once = false

-- Long enough for the client to observe this state as occupied, short enough that
-- stalling this event thread does not starve the rest of the load.
local hold_seconds = 0.5

if test_directory then
    shutdown_path = test_directory .. '/lua-shutdown-done'
    active_path = test_directory .. '/lua-remap-state.active'
end

function do_remap()
    ts.debug('do_remap called')

    if shutdown_path then
        local done = io.open(shutdown_path, 'r')

        if done then
            done:close()
            ts.debug('do_remap ran after __shutdown__')
        end
    end

    local uri = ts.client_request.get_uri()

    if uri == '/remap-hello' then
        ts.http.set_resp(200, 'Remap Lua response')
        return
    end

    if not active_path or uri ~= '/remap-hold' then
        return
    end

    if held_once then
        -- Answer from here rather than going to the origin, so the load stays
        -- pointed at this Lua state.
        ts.http.set_resp(200, 'Remap Lua queued')
        return
    end

    held_once = true

    local active = assert(io.open(active_path, 'w'))

    active:write('active')
    active:close()

    local deadline = ts.now() + hold_seconds

    while ts.now() < deadline do
    end

    os.remove(active_path)
    ts.http.set_resp(200, 'Remap Lua hold')
end
