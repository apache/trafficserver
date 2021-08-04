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


ts.add_package_path('/usr/local/var/lua/?.lua')

local msc = require("msc")
local msc_config = require("msc_config")
local ffi = require("ffi")
local C = require("C")

local mst = msc.msc_init()
msc.msc_set_connector_info(mst, "ModSecurity-ats")

-- Initialization. Load the modsecurity configuration passed to this lua module
function __init__(argtb)
  if (#argtb) < 1 then
    ts.error("No ModSecurity Conf is given")
    return -1
  end

  msc_config.rulesfile = argtb[1]	
  ts.debug("ModSecurity Conf file is " .. msc_config.rulesfile)

  msc_config.rules = msc.msc_create_rules_set()
  local error = ffi.new("const char *[1]")
  local result = msc.msc_rules_add_file(msc_config.rules, msc_config.rulesfile, error)
  if(result < 0) then
    ts.error("Problems loading the rules: ".. ffi.string(error[0]))
    msc.msc_rules_cleanup(msc_config.rules)
    msc_config.rules = nil
    return -1
  end

end

-- Reload modsecurity configuration. Trigger by "traffic_ctl config reload"
function __reload__()
  ts.debug("Reloading ModSecurity Conf: " .. msc_config.rulesfile)

  newrules = msc.msc_create_rules_set()
  local error = ffi.new("const char *[1]")
  local result = msc.msc_rules_add_file(newrules, msc_config.rulesfile, error)
  if(result < 0) then
    ts.error("Problems loading the rules during reload: ".. ffi.string(error[0]))

    msc.msc_rules_cleanup(newrules)
    newrules = nil
  else
    -- TODO: we are not doing clean up on the old rules and thus leaking resources here
    msc_config.rules = newrules
  end

end

-- Entry point function run for each incoming request
function do_global_read_request()
  if(msc_config.rules == nil) then
    ts.debug("No rules loaded. Thus there is no processing done")
    return 0
  end
  local txn = msc.msc_new_transaction(mst, msc_config.rules ,nil)

  -- processing for the connection information
  local client_ip, client_port, client_ip_family = ts.client_request.client_addr.get_addr()
  local incoming_port = ts.client_request.client_addr.get_incoming_port()
  msc.msc_process_connection(txn, client_ip, client_port, "127.0.0.1", incoming_port)

  -- processing for the uri information
  local uri = ts.client_request.get_uri()
  local query_params = ts.client_request.get_uri_args() or ''
  if (query_params ~= '') then
    uri = uri .. '?' .. query_params
  end
  msc.msc_process_uri(txn, uri, ts.client_request.get_method(), ts.client_request.get_version())

  -- processing for the request headers
  local hdrs = ts.client_request.get_headers()
  for k, v in pairs(hdrs) do
    msc.msc_add_request_header(txn, k, v)
  end
  msc.msc_process_request_headers(txn)
  msc.msc_process_request_body(txn)
  ts.debug("done with processing request")

  -- detect if intervention is needed
  local iv = ffi.new("ModSecurityIntervention")
  iv.status = 200
  iv.log = nil
  iv.url = nil
  iv.disruptive = 0
  local iv_res = msc.msc_intervention(txn, iv)
  ts.debug("done with intervention ".. iv_res .. ' with status ' .. iv.status )

  if(iv.log ~= nil) then
    ts.debug("Intervention log: " .. ffi.string(iv.log))
    C.free(iv.log)
  end

  -- if found an intervention url, trigger handler when sending response to client
  if(iv.url ~= nil) then
    ts.ctx['url'] = ffi.string(iv.url)
    ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
    C.free(iv.url)
  end

  -- intervention is needed if status is not 200
  if (iv.status ~= 200) then
    ts.http.set_resp(iv.status)
    msc.msc_process_logging(txn)
    msc.msc_transaction_cleanup(txn)
    ts.debug("done with setting custom response")
    return 0
  end

  -- storing modsecurity object in context
  ts.ctx["mst"] = txn
  ts.debug("done with setting context")

  -- trigger handler to run when response is received
  ts.hook(TS_LUA_HOOK_READ_RESPONSE_HDR, read_response)

  return 0
end

-- function run when response is received from origin
function read_response()
  -- retrieving modsecurity object
  local txn = ts.ctx["mst"]

  if(txn == nil) then
    ts.error("no transaction object")
    return 0
  end

  -- processing for the response headers
  local hdrs = ts.server_response.get_headers()
  for k, v in pairs(hdrs) do
    msc.msc_add_response_header(txn, k, v)
  end
  msc.msc_process_response_headers(txn, ts.server_response.get_status(), "HTTP/"..ts.server_response.get_version())
  msc.msc_process_response_body(txn)
  ts.debug("done with processing response")

  -- determine if intervention is needed
  local iv = ffi.new("ModSecurityIntervention")
  iv.status = 200
  iv.log = nil
  iv.url = nil
  iv.disruptive = 0
  local iv_res = msc.msc_intervention(txn, iv)
  ts.debug("done with intervention ".. iv_res .. ' with status ' .. iv.status )

  if(iv.log ~= nil) then
    ts.debug("Intervention log: " .. ffi.string(iv.log))
    C.free(iv.log)
  end

  -- if found an intervention url, trigger handler when sending response to client
  ts.ctx['url'] = ''
  if(iv.url ~= nil) then
    ts.ctx['url'] = ffi.string(iv.url)
    C.free(iv.url)
  end

  -- intervention needed when status is not 200
  ts.ctx['status'] = nil
  if (iv.status ~= 200) then
    ts.ctx['status'] = iv.status
  end

  -- response needs to be modified?
  if(ts.ctx['url'] ~= '' or ts.ctx['status'] ~= nil) then
    ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
  end

  -- we need to return -1 for lua to reset response body with an error body
  if(ts.ctx['status'] ~= nil) then
    ts.ctx["mst"] = nil
    msc.msc_process_logging(txn)
    msc.msc_transaction_cleanup(txn)
    ts.debug("done with cleaning up context and return error response")
    return -1
  end

  -- cleaning up
  ts.ctx["mst"] = nil
  msc.msc_process_logging(txn)
  msc.msc_transaction_cleanup(txn)
  ts.debug("done with cleaning up context")

  return 0
end

-- function run when sending response to client
function send_response()
  -- retrieve intervention url and add it as "Location" header on response to client
  local location = ts.ctx['url']
  if (location ~= '') then
    ts.debug('location: ' .. location)
    ts.client_response.header['Location'] = location
  end

  -- retrieve status and reset the response with it
  local status = ts.ctx['status']
  if (status ~= nil) then
    ts.client_response.set_error_resp(status, 'Contents Reset by ModSecurity\n')
  end
end
