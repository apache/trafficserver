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

-- This example depends on "luajit-geoip".
-- It illustrates how to connect to GeoIP and use it to look up country of an IP address.
-- It can be used in plugin.config with the lua plugin.

-- Setup Instructions
-- 1) install GeoIP - 1.6.12
-- 2) install GeoIP legacy country database - https://dev.maxmind.com/geoip/legacy/install/country/
-- 3) install luajit-geoip (https://github.com/leafo/luajit-geoip)
--    or just copy geoip/init.lua from the repo to /usr/local/share/lua/5.1/geoip/init.lua
-- 4) You may need to make change so luajit-geoip does ffi.load() on /usr/local/lib/libGeoIP.so

ts.add_package_path("/usr/local/share/lua/5.1/?.lua")

local geoip = require "geoip"

function do_global_send_response()
  local res = geoip.lookup_addr("8.8.8.8")
  ts.client_response.header["X-Country"] = res.country_code
end
