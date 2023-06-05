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


function request_client(data, eos)
  ts.debug('request_client')
  ts.ctx['reqbody'] = ts.ctx['reqbody'] .. data
  ts.debug("req transform got " .. string.len(data) .. "bytes, eos=" .. eos)
  if (eos == 1) then
    ts.debug('End of Stream and the reqbody is ... ')
    ts.debug(ts.ctx['reqbody'])
  end
end

function do_global_read_request()
  ts.debug('do_global_read_request')
  if (ts.client_request.get_method() == 'POST') then
    ts.debug('post')
    ts.ctx['reqbody'] = ''
    ts.hook(TS_LUA_REQUEST_CLIENT, request_client)
  end
end
