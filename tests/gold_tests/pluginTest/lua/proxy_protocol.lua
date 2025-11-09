--  Licensed to the Apache Software Foundation (ASF) under one
--  or more contributor license agreements.  See the NOTICE file
--  distributed with this work for additional information
--  regarding copyright ownership.  The ASF licenses this file
--  to you under the Apache License, Version 2.0 (the
--  "License"); you may not use this file except in compliance
--  with the License.  You may obtain a copy of the License at
--
--      http://www.apache.org/licenses/LICENSE-2.0
--
--  Unless required by applicable law or agreed to in writing, software
--  distributed under the License is distributed on an "AS IS" BASIS,
--  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
--  See the License for the specific language governing permissions and
--  limitations under the License.

function do_remap()
  local req = ts.client_request
  
  -- Get PROXY protocol version
  local pp_version = req.get_pp_info_int(TS_LUA_PP_INFO_VERSION)
  if pp_version then
    ts.debug(string.format("PP-Version: %d", pp_version))
    
    -- Get source address and port
    local src_addr = req.get_pp_info(TS_LUA_PP_INFO_SRC_ADDR)
    local src_port = req.get_pp_info_int(TS_LUA_PP_INFO_SRC_PORT)
    
    -- Get destination address and port
    local dst_addr = req.get_pp_info(TS_LUA_PP_INFO_DST_ADDR)
    local dst_port = req.get_pp_info_int(TS_LUA_PP_INFO_DST_PORT)
    
    -- Get protocol and socket type
    local protocol = req.get_pp_info_int(TS_LUA_PP_INFO_PROTOCOL)
    local sock_type = req.get_pp_info_int(TS_LUA_PP_INFO_SOCK_TYPE)
    
    if src_addr and src_port then
      ts.debug(string.format("PP-Source: %s:%d", src_addr, src_port))
    end
    
    if dst_addr and dst_port then
      ts.debug(string.format("PP-Destination: %s:%d", dst_addr, dst_port))
    end
    
    if protocol then
      ts.debug(string.format("PP-Protocol: %d", protocol))
    end
    
    if sock_type then
      ts.debug(string.format("PP-SocketType: %d", sock_type))
    end
    
    -- Add custom header with PP info
    if src_addr then
      ts.client_request.header['X-PP-Client-IP'] = src_addr
    end
  else
    ts.debug("PP-Not-Present")
  end
  
  return 0
end
