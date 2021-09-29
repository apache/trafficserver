function send_request()
local server_name = ts.server_request.server_addr.get_nexthop_name()
local server_ip_will_connect = ts.server_request.server_addr.get_ip()
local server_ip_should_connect = ts.host_lookup(server_name)

if server_ip_will_connect ~= server_ip_should_connect then
ts.server_request.header['Connection'] = 'dummy'
end
ts.server_request.header['Connection'] = 'close'
end

function do_remap()
ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
end
