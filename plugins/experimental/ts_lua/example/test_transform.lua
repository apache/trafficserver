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


local APPEND_DATA = 'TAIL\n'

function append_transform(data, eos)
    if ts.ctx['len_set'] == nil then
        local sz = ts.http.resp_transform.get_upstream_bytes()

        if sz ~= TS_LUA_INT64_MAX then
            ts.http.resp_transform.set_downstream_bytes(sz + string.len(APPEND_DATA))
        end

        ts.ctx['len_set'] = true
    end

    if eos == 1 then
        return data .. APPEND_DATA, eos

    else
        return data, eos
    end
end


function do_remap()
    ts.hook(TS_LUA_RESPONSE_TRANSFORM, append_transform)
    ts.http.resp_cache_transformed(0)
    ts.http.resp_cache_untransformed(1)
    return 0
end
