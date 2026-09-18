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

function send_response()
    -- Set Water Mark of Input Buffer
    ts.http.resp_transform.set_upstream_watermark_bytes(31337)
    local wm = ts.http.resp_transform.get_upstream_watermark_bytes()
    ts.debug(string.format('WMbytes(%d)', wm))
    return 0
end

function do_remap()
    local req_host = ts.client_request.header.Host

    if req_host == nil then
        return 0
    end

    ts.hook(TS_LUA_RESPONSE_TRANSFORM, send_response)
    ts.http.resp_cache_transformed(0)
    ts.http.resp_cache_untransformed(1)
    return 0
end
