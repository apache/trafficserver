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

local function as_text(value)
    if value == nil then return "<nil>" end
    if type(value) == "table" then return "<table>" end
    return tostring(value)
end

local function record_early_server_request_values()
    ts.ctx["early_header"]       = as_text(ts.server_request.header["Host"])
    ts.ctx["early_header_table"] = as_text(ts.server_request.header_table["Host"])
    ts.ctx["early_headers"]      = as_text(ts.server_request.get_headers())
    ts.ctx["early_uri"]          = as_text(ts.server_request.get_uri())
    ts.ctx["early_uri_args"]     = as_text(ts.server_request.get_uri_args())
    ts.ctx["early_method"]       = as_text(ts.server_request.get_method())
    ts.ctx["early_url_host"]     = as_text(ts.server_request.get_url_host())
    ts.ctx["early_url_scheme"]   = as_text(ts.server_request.get_url_scheme())
    ts.ctx["early_version"]      = as_text(ts.server_request.get_version())

    ts.server_request.header["X-Early-Server-Request"] = "ignored"
    ts.server_request.set_uri("/ignored")
    ts.server_request.set_uri_args("ignored=true")
    ts.server_request.set_method("POST")
    ts.server_request.set_url_host("ignored.example")
    ts.server_request.set_url_scheme("https")
    ts.server_request.set_version("1.0")
end

function send_response()
    ts.client_response.header["Early-Server-Header"]       = ts.ctx["early_header"]
    ts.client_response.header["Early-Server-Header-Table"] = ts.ctx["early_header_table"]
    ts.client_response.header["Early-Server-Headers"]      = ts.ctx["early_headers"]
    ts.client_response.header["Early-Server-Uri"]          = ts.ctx["early_uri"]
    ts.client_response.header["Early-Server-Uri-Args"]     = ts.ctx["early_uri_args"]
    ts.client_response.header["Early-Server-Method"]       = ts.ctx["early_method"]
    ts.client_response.header["Early-Server-Url-Host"]     = ts.ctx["early_url_host"]
    ts.client_response.header["Early-Server-Url-Scheme"]   = ts.ctx["early_url_scheme"]
    ts.client_response.header["Early-Server-Version"]      = ts.ctx["early_version"]
end

function do_remap()
    record_early_server_request_values()
    ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)

    return 0
end
