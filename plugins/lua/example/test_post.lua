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


function post_transform(data, eos)
    local already = ts.ctx['body']
    ts.ctx['body'] = already..string.upper(data)
    return data, eos
end

function send_response()
    ts.client_response.header['Method'] = ts.ctx['method']
    if ts.ctx['body'] ~= nil then
        ts.client_response.header['Body'] = ts.ctx['body']
    end
    return 0
end


function do_remap()
    local req_method = ts.client_request.get_method()

    ts.ctx['method'] = req_method
    ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)

    if req_method ~= 'POST' then
        return 0
    end

    ts.ctx['body'] = ''
    ts.hook(TS_LUA_REQUEST_TRANSFORM, post_transform)

    return 0
end

