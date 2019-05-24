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


-- This script is for sorting query parameters on incoming requests before doing cache lookup
-- so we can get better cache hit ratio
-- It can be used in remap.config for a remap rule with the lua plugin.

function pairsByKeys (t, f)
  local a = {}
  for n in pairs(t) do table.insert(a, n) end
  table.sort(a, f)
  local i = 0      -- iterator variable
  local iter = function ()   -- iterator function
    i = i + 1
    if a[i] == nil then return nil
    else return a[i], t[a[i]]
    end
  end
  return iter
end

function do_remap()
  t = {} 
  s = ts.client_request.get_uri_args() or ''
  -- Original String
  i = 1
  for k, v in string.gmatch(s, "([0-9a-zA-Z-_]+)=([0-9a-zA-Z-_]+)") do
    t[k] = v
  end

  output = ''
  for name, line in pairsByKeys(t) do
    output = output .. '&' .. name .. '=' .. line
  end
  output = string.sub(output, 2)
  -- Modified String 
  ts.client_request.set_uri_args(output)
  return 0
end 
