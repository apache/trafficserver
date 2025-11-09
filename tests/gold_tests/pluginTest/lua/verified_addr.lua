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

function do_remap()
    local result = ""
    
    -- Test 1: Check if verified address is initially nil
    local ip1, family1 = ts.client_request.client_addr.get_verified_addr()
    if not ip1 then
        result = result .. "initial:nil;"
    end
    
    -- Test 2: Set an IPv4 verified address from X-Real-IP header
    local real_ip = ts.client_request.header["X-Real-IP"]
    if real_ip then
        local success, err = pcall(function()
            ts.client_request.client_addr.set_verified_addr(real_ip, 2)
        end)
        
        if success then
            result = result .. "set:success;"
            
            -- Test 3: Get the verified address we just set
            local ip2, family2 = ts.client_request.client_addr.get_verified_addr()
            if ip2 then
                result = result .. "get:" .. ip2 .. ":" .. tostring(family2) .. ";"
            else
                result = result .. "get:failed;"
            end
        else
            result = result .. "set:failed;"
        end
    end
    
    -- Test 4: Try setting an IPv6 address from X-Real-IP-V6 header
    local real_ipv6 = ts.client_request.header["X-Real-IP-V6"]
    if real_ipv6 then
        local success, err = pcall(function()
            ts.client_request.client_addr.set_verified_addr(real_ipv6, 10)
        end)
        
        if success then
            result = result .. "setv6:success;"
            
            -- Get the IPv6 verified address
            local ip3, family3 = ts.client_request.client_addr.get_verified_addr()
            if ip3 then
                result = result .. "getv6:" .. ip3 .. ":" .. tostring(family3) .. ";"
            else
                result = result .. "getv6:failed;"
            end
        else
            result = result .. "setv6:failed;"
        end
    end
    
    -- Test 5: Try setting an invalid address (should fail)
    local invalid_ip = ts.client_request.header["X-Invalid-IP"]
    if invalid_ip then
        local success, err = pcall(function()
            ts.client_request.client_addr.set_verified_addr(invalid_ip, 2)
        end)
        
        if not success then
            result = result .. "invalid:rejected;"
        else
            result = result .. "invalid:accepted;"
        end
    end
    
    -- Return the result in the response
    ts.http.set_resp(200, result)
    return 0
end
