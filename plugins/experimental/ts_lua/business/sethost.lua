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


local HOSTNAME = ''

function __init__(argtb)

    if (#argtb) < 1 then
        print(argtb[0], 'hostname parameter required!!')
        return -1
    end

    HOSTNAME = argtb[1]
end

function do_remap()
    local req_host = ts.client_request.header.Host

    if req_host == nil then
        return 0
    end

    ts.client_request.header['Host'] = HOSTNAME

    return 0
end

