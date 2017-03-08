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

function send_data()
    local nt = os.time()..' Zheng.\n'

    local resp =  'HTTP/1.1 200 OK\r\n' ..
        'Server: ATS/3.2.0\r\n' ..
        'Content-Type: text/plain\r\n' ..
        'Content-Length: ' .. string.len(nt) .. '\r\n' ..
        'Last-Modified: ' .. os.date("%a, %d %b %Y %H:%M:%S GMT", os.time()) .. '\r\n' ..
        'Connection: keep-alive\r\n' ..
        'Cache-Control: max-age=7200\r\n' ..
        'Accept-Ranges: bytes\r\n\r\n' ..
        nt

    ts.sleep(1)
    ts.say(resp)
end


function do_remap()
    ts.http.intercept(send_data)
    return 0
end

