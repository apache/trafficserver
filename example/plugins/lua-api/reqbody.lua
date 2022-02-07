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

-- This example illustrates how to do request body transform.
-- It stores the request body and prints it at the end of the transform.

function request_transform(data, eos)
  ts.ctx['reqbody'] = ts.ctx['reqbody'] .. data

  if ts.ctx['len_set'] == nil then
    ts.debug("len not set")
    local sz = ts.http.req_transform.get_downstream_bytes()
    ts.debug("len "..sz)
    ts.http.req_transform.set_upstream_bytes(sz)
    ts.ctx['len_set'] = true
  end

  ts.debug("req transform got " .. string.len(data) .. "bytes, eos=" .. eos)

  if (eos == 1) then
    ts.debug('End of Stream and the reqbody is ... ')
    ts.debug(ts.ctx['reqbody'])
  end

  return data, eos
end

function do_remap()
  ts.debug('do_remap')
  if (ts.client_request.get_method() == 'POST') then
    ts.ctx['reqbody'] = ''
    ts.hook(TS_LUA_REQUEST_TRANSFORM, request_transform)
  end

  return 0
end
