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

require 'os'

function process_combo(host)
    local url1 = string.format('http://%s/css/1.css', host)
    local url2 = string.format('http://%s/css/2.css', host)
    local url3 = string.format('http://%s/css/3.css', host)

    local hdr = {
        ['Host'] = host,
        ['User-Agent'] = 'blur blur',
    }

    local ct = {
        header = hdr,
        method = 'GET'
    }

    local arr = ts.fetch_multi(
            {
                {url1, ct},
                {url2, ct},
                {url3, ct},
            })

    local ctype = arr[1].header['Content-Type']
    local body = arr[1].body .. arr[2].body .. arr[3].body

    local resp =  'HTTP/1.1 200 OK\r\n' ..
                  'Server: ATS/5.2.0\r\n' ..
                  'Last-Modified: ' .. os.date("%a, %d %b %Y %H:%M:%S GMT", os.time()) .. '\r\n' ..
                  'Cache-Control: max-age=7200\r\n' ..
                  'Accept-Ranges: bytes\r\n' ..
                  'Content-Type: ' .. ctype .. '\r\n' ..
                  'Content-Length: ' .. string.format('%d', string.len(body)) .. '\r\n\r\n' ..
                  body

    ts.say(resp)
end

function do_remap()
    local inner =  ts.http.is_internal_request()
    if inner ~= 0 then
        return 0
    end

    local h = ts.client_request.header['Host']
    ts.http.intercept(process_combo, h)
end
