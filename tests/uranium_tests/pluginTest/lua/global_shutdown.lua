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

-- The test directory doubles as the handshake area with shutdown_race_client.py.
-- Without it this script only exercises the plain __shutdown__ path.
local test_directory = os.getenv('TS_LUA_SHUTDOWN_TEST_DIR')

-- Which Lua state this copy of the script was loaded into. The script is loaded
-- once per state, in ascending state order, so a load-time counter names the
-- states the same way the plugin does.
local state_id = 0

-- The state that occupies itself with /hold requests. State 0 is the state whose
-- __shutdown__ runs first, so keeping the busy work out of state 0 is what makes
-- this a cross-state overlap rather than a same-state one.
local hold_state_id = 1

-- Written while hold_state_id is inside do_global_read_request.
local active_path = nil

-- Written once the __shutdown__ functions have run. Any Lua executed after that
-- is Lua running against whatever __shutdown__ tore down.
local shutdown_path = nil

-- How long a /hold request occupies its Lua state. Long enough that the state
-- stays busy across a shutdown, short enough to keep the load flowing.
local hold_seconds = 0.15

-- How long __shutdown__ spends releasing resources, standing in for a script that
-- does real cleanup work there.
local shutdown_seconds = 1.0

if test_directory then
    local counter_path = test_directory .. '/lua-state-counter'
    local counter = io.open(counter_path, 'r')

    if counter then
        state_id = tonumber(counter:read('*a')) or 0
        counter:close()
    end

    counter = assert(io.open(counter_path, 'w'))
    counter:write(state_id + 1)
    counter:close()

    active_path = test_directory .. '/lua-state-' .. hold_state_id .. '.active'
    shutdown_path = test_directory .. '/lua-shutdown-done'
end

function do_global_read_request()
    ts.debug('do_global_read_request called')

    if shutdown_path then
        local done = io.open(shutdown_path, 'r')

        if done then
            done:close()
            ts.debug('do_global_read_request ran after __shutdown__')
        end
    end

    if not active_path or state_id ~= hold_state_id or ts.client_request.get_uri() ~= '/hold' then
        return
    end

    local active = assert(io.open(active_path, 'w'))

    active:write('active')
    active:close()

    -- Busy-wait: ts.sleep() would yield and release the Lua state mutex, and
    -- holding that mutex is the whole point of this callback.
    local deadline = ts.now() + hold_seconds

    while ts.now() < deadline do
    end

    os.remove(active_path)
end

function __shutdown__()
    if active_path and state_id == 0 then
        local active = io.open(active_path, 'r')

        if active then
            active:close()
            ts.debug('__shutdown__ overlapped an active Lua state')
        end
    end

    ts.debug('__shutdown__ called for state ' .. state_id)

    if not shutdown_path then
        return
    end

    local done = assert(io.open(shutdown_path, 'w'))

    done:write('done')
    done:close()

    if state_id ~= 0 then
        return
    end

    -- A real __shutdown__ releases resources rather than returning immediately.
    -- Spending that time here is what gives the request load a chance to pile up
    -- on the Lua state mutexes the barrier is holding: those requests must not
    -- enter Lua afterwards, in a global or in a remap state.
    local deadline = ts.now() + shutdown_seconds

    while ts.now() < deadline do
    end
end
