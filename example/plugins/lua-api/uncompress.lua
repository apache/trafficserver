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


-- This example depends on "lua-zlib". 
-- It uncompresses a gzipped content body and prints it out in debug log.
-- It can be added in remap.config for a remap rule with the lua plugin.

-- Setup Instructions
-- 1) install lua-zlib - v1.2

ts.add_package_cpath('/usr/lib/lua/5.1/?.so')

local zlib = require "zlib"

function upper_transform(data, eos)
    ts.ctx['text'] = ts.ctx['text'] .. data

    if eos ==1 then
      local stream = zlib.inflate()
      local inflated, eof, bytes_in, bytes_out = stream(ts.ctx['text'])
      if (eof == true) then
         ts.debug("==== eof ====")
      end
      ts.debug("==== bytes_in: "..(bytes_in or ''))
      ts.debug("==== bytes_out:"..(bytes_out or ''))
      ts.debug("==== uncompressed data begin ===")
      ts.debug(inflated or 'no data')
      ts.debug("==== uncompressed data end ===")
    end

    return string.upper(data), eos
end

function do_remap()
    ts.hook(TS_LUA_RESPONSE_TRANSFORM, upper_transform)
    ts.ctx['text'] = ''
    return 0
end
