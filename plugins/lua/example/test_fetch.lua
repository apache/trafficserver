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
    if ts.ctx['flen'] ~= nil then
        ts.client_response.header['Flen'] = ts.ctx['flen']
    end
end

function post_remap()
    local url = string.format('http://%s/foo.txt', ts.ctx['host'])
    local hdr = {
        ['Host'] = ts.ctx['host'],
        ['User-Agent'] = 'dummy',
    }

    local res = ts.fetch(url, {method = 'GET', header=hdr})

    if res.status == 200 then
        ts.ctx['flen'] = string.len(res.body)
        print(res.body)
    end
end


function do_remap()
    local inner = ts.http.is_internal_request()
    if inner ~= 0 then
        return 0
    end

    local host = ts.client_request.header['Host']
    ts.ctx['host'] = host

    ts.hook(TS_LUA_HOOK_POST_REMAP, post_remap)
    ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
end

