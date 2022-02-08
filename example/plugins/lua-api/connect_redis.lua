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

-- This example depends on "redis-lua" 2.0.4 - https://github.com/nrk/redis-lua
-- And redis-lua depends on LuaSocket v3.0-rc1 - https://github.com/diegonehab/luasocket
-- It illustrates how to connect to redis and retrieve a key value.
-- It can be used in plugin.config with the lua plugin.

-- Compile luasocket with luajit library and installation:
-- 1. wget https://github.com/diegonehab/luasocket/archive/v3.0-rc1.tar.gz
-- 2. tar zxf v3.0-rc1.tar.gz
-- 3. cd luasocket-3.0-rc1
-- 4. sed -i "s/LDFLAGS_linux=-O -shared -fpic -o/LDFLAGS_linux=-O -shared -fpic -L\/usr\/lib -lluajit-5.1 -o/" src/makefile
-- 5. ln -sf /usr/lib/libluajit-5.1.so.2.1.0 /usr/lib/libluajit-5.1.so
-- 6. mkdir -p /usr/include/lua
-- 7. ln -sf /usr/include/luajit-2.1 /usr/include/lua/5.1
-- 8. make
-- 9. make install-unix

-- redis-lua installation:
-- 1. wget https://github.com/nrk/redis-lua/archive/v2.0.4.tar.gz
-- 2. tar zxf v2.0.4.tar.gz
-- 3. mkdir -p /usr/local/share/lua/5.1
-- 4. cp redis-lua-2.0.4/src/redis.lua /usr/local/share/lua/5.1/redis.lua

-- Redis setup instructions:
-- Unix domain socket has better performance and so we should set up local redis to use that.
-- Note the sock must be readable/writable by nobody since ATS runs as that user.
-- Sample instructions for setting up redis and putting a key in
-- 1. edit /etc/redis/redis.conf (or copy from redis configuration file). Make the following changes
--   a. "port 0"
--   b. "unixsocket /var/run/redis/redis.sock"
--   c. "unixsocketperm 755"
-- 2. sudo chown nobody /var/run/redis
-- 3. sudo chgrp nogroup /var/run/redis
-- 4. sudo chown nobody /var/log/redis
-- 5. sudo chgrp nogroup /var/log/redis
-- 6. sudo -u nobody redis-server /etc/redis/redis.conf
-- 7. sudo -u nobody redis-cli -s /var/run/redis/redis.sock set mykey helloworld

ts.add_package_cpath("/usr/local/lib/lua/5.1/?.so;/usr/local/lib/lua/5.1/socket/?.so;/usr/local/lib/lua/5.1/mime/?.so")
ts.add_package_path("/usr/local/share/lua/5.1/?.lua;/usr/local/share/lua/5.1/socket/?.lua")

local redis = require "redis"
-- not connecting to redis default port
-- local client = redis.connect('127.0.0.1', 6379)

-- connecting to unix domain socket
local client = redis.connect("unix:///var/run/redis/redis.sock")

function do_global_send_response()
  local response = client:ping()
  local value = client:get("mykey")
  ts.client_response.header["X-Redis-Ping"] = tostring(response)
  ts.client_response.header["X-Redis-MyKey"] = value
end
