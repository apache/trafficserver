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


local module = {}

function module.test()
  return 0
end

function module.set_hook()
    ts.hook(TS_LUA_HOOK_TXN_CLOSE, module.test)
end


function module.set_context()
    ts.ctx['test'] = 'test10'
end

function module.check_internal()
    return ts.http.is_internal_request()
end

function module.return_constant()
    return TS_LUA_REMAP_DID_REMAP
end

function module.split(s, delimiter)
    result = {}
    for match in (s..delimiter):gmatch("(.-)"..delimiter) do
        table.insert(result, match)
    end
    return result
end

return module
