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
-- It illustrates how to connect to GeoIP and uses it to look up country of an IP address.
-- It can be used in plugin.config with the lua plugin.

-- Setup Instructions
-- 1. install legacy GeoIP library 1.6.12 (https://github.com/maxmind/geoip-api-c)
--   a. wget https://github.com/maxmind/geoip-api-c/releases/download/v1.6.12/GeoIP-1.6.12.tar.gz
--   b. tar zxvf GeoIP-1.6.12.tar.gz
--   c. cd GeoIP-1.6.12
--   d. ./configure; make; make install
-- 2. Find and install GeoIP legacy country database to /usr/local/share/GeoIP/GeoIP.dat
-- 3. install luajit-geoip v2.1.0 (https://github.com/leafo/luajit-geoip)
--   a. wget https://github.com/leafo/luajit-geoip/archive/refs/tags/v2.1.0.tar.gz
--   b. tar zxvf v2.1.0.tar.gz
--   c. mkdir -p /usr/local/share/lua/5.1/geoip
--   d. cp luajit-geoip-2.1.0/geoip.lua /usr/local/share/lua/5.1/geoip.lua
--   e. cp luajit-geoip-2.1.0/geoip/*.lua /usr/local/share/lua/5.1/geoip/

ts.add_package_path('/usr/local/share/lua/5.1/?.lua')

local geoip = require 'geoip'

function do_global_send_response()
  local res = geoip.lookup_addr("8.8.8.8")
  ts.client_response.header['X-Country'] = res.country_code
end
