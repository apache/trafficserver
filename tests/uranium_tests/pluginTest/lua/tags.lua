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
  if 'GET' == ts.client_request.get_method() then
    local msg = ""
    if ts.is_debug_tag_set() then
      msg = msg .. " default"
    end
    if ts.is_debug_tag_set("foo") then
      msg = msg .. " foo"
    end
    if ts.is_debug_tag_set("ts_lua") then
      msg = msg .. " ts_lua"
    end
    ts.http.set_resp(200, msg)
  end
end
