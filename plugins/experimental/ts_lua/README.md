Name
======

ts-lua - Embed the Power of Lua into TrafficServer.

Status
======
This module is being tested under our production environment.

Version
======
ts-lua has not been released yet.

Synopsis
======

**test_hdr.lua**

    function send_response()
        ts.client_response.header['Rhost'] = ts.ctx['rhost']
        return 0
    end


    function do_remap()
        local req_host = ts.client_request.header.Host

        if req_host == nil then
            return 0
        end

        ts.ctx['rhost'] = string.reverse(req_host)

        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)

        return 0
    end



**test_transform.lua**

    function upper_transform(data, eos)
        if eos == 1 then
            return string.upper(data)..'S.H.E.\n', eos
        else
            return string.upper(data), eos
        end
    end

    function send_response()
        ts.client_response.header['SHE'] = ts.ctx['tb']['she']
        return 0
    end


    function do_remap()
        local req_host = ts.client_request.header.Host

        if req_host == nil then
            return 0
        end

        ts.ctx['tb'] = {}
        ts.ctx['tb']['she'] = 'wo ai yu ye hua'

        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        ts.hook(TS_LUA_RESPONSE_TRANSFORM, upper_transform)

        ts.http.resp_cache_transformed(0)
        ts.http.resp_cache_untransformed(1)
        return 0
    end



**test_cache_lookup.lua**

    function send_response()
        ts.client_response.header['Rhost'] = ts.ctx['rhost']
        ts.client_response.header['CStatus'] = ts.ctx['cstatus']
    end


    function cache_lookup()
        local cache_status = ts.http.get_cache_lookup_status()
        ts.ctx['cstatus'] = cache_status
    end


    function do_remap()
        local req_host = ts.client_request.header.Host

        if req_host == nil then
            return 0
        end

        ts.ctx['rhost'] = string.reverse(req_host)

        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)

        return 0
    end



**test_ret_403.lua**

    function send_response()
        ts.client_response.header['Now'] = ts.now()
        return 0
    end


    function do_remap()

        local uri = ts.client_request.get_uri()

        pos, len = string.find(uri, '/css/')
        if pos ~= nil then
            ts.http.set_resp(403, "Document access failed :)\n")
            return 0
        end

        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)

        return 0
    end



**sethost.lua**

    HOSTNAME = ''

    function __init__(argtb)

        if (#argtb) < 1 then
            print(argtb[0], 'hostname parameter required!!')
            return -1
        end

        HOSTNAME = argtb[1]
    end

    function do_remap()
        local req_host = ts.client_request.header.Host

        if req_host == nil then
            return 0
        end

        ts.client_request.header['Host'] = HOSTNAME

        return 0
    end


**test_intercept.lua**

    require 'os'

    function send_data()
        local nt = os.time()..' Zheng.\n'
        local resp =  'HTTP/1.1 200 OK\r\n' ..
            'Server: ATS/3.2.0\r\n' ..
            'Content-Type: text/plain\r\n' ..
            'Content-Length: ' .. string.len(nt) .. '\r\n' ..
            'Last-Modified: ' .. os.date("%a, %d %b %Y %H:%M:%S GMT", os.time()) .. '\r\n' ..
            'Connection: keep-alive\r\n' ..
            'Cache-Control: max-age=7200\r\n' ..
            'Accept-Ranges: bytes\r\n\r\n' ..
            nt

        ts.sleep(1)
        return resp
    end

    function do_remap()
        ts.http.intercept(send_data)
        return 0
    end


**test_server_intercept.lua**

    require 'os'

    function send_data()
        local nt = os.time()..'\n'
        local resp =  'HTTP/1.1 200 OK\r\n' ..
            'Server: ATS/3.2.0\r\n' ..
            'Content-Type: text/plain\r\n' ..
            'Content-Length: ' .. string.len(nt) .. '\r\n' ..
            'Last-Modified: ' .. os.date("%a, %d %b %Y %H:%M:%S GMT", os.time()) .. '\r\n' ..
            'Connection: keep-alive\r\n' ..
            'Cache-Control: max-age=30\r\n' ..
            'Accept-Ranges: bytes\r\n\r\n' ..
            nt
        return resp
    end

    function do_remap()
        ts.http.server_intercept(send_data)
        return 0
    end


Description
======
This module embeds Lua, via the standard Lua 5.1 interpreter, into Apache Traffic Server. This module acts as remap plugin of Traffic Server, so we should realize **'do_remap'** function in each lua script. We can write this in remap.config:

map http://a.tbcdn.cn/ http://inner.tbcdn.cn/ @plugin=/usr/lib64/trafficserver/plugins/libtslua.so @pparam=/etc/trafficserver/script/test_hdr.lua

Sometimes we want to receive parameters and process them in the script, we should realize **'\__init__'** function in the lua script(sethost.lua is a reference), and we can write this in remap.config:

map http://a.tbcdn.cn/ http://inner.tbcdn.cn/ @plugin=/usr/lib64/trafficserver/plugins/libtslua.so @pparam=/etc/trafficserver/script/sethost.lua @pparam=img03.tbcdn.cn



TS API for Lua
======
Introduction
------
The API is exposed to Lua in the form of one standard packages ts. This package is in the default global scope and is always available within lua script.



ts.now
------
**syntax**: *val = ts.now()*

**context**: global

**description**: This function returns the time since the Epoch (00:00:00 UTC, January 1, 1970), measured in seconds.

Here is an example:

    function send_response()
        ts.client_response.header['Now'] = ts.now()
        return 0
    end


ts.debug
------
**syntax**: *ts.debug(MESSAGE)*

**context**: global

**description**: Log the MESSAGE to traffic.out if debug is enabled.

Here is an example:

    function do_remap()
       ts.debug('I am in do_remap now.')
       return 0
    end
    
The debug tag is ts_lua and we should write this in records.config:
    
    CONFIG proxy.config.diags.debug.tags STRING ts_lua
    

ts.hook
------
**syntax**: *ts.hook(HOOK_POINT, FUNCTION)*

**context**: do_remap or later

**description**: Hooks are points in http transaction processing where we can step in and do some work.
FUNCTION will be called when the http transaction steps in to HOOK_POINT.

Here is an example:

    function send_response()
        s.client_response.header['SHE'] = 'belief'
    end
    
    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
    end

Hook point constants
------
**context**: do_remap or later

    TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE
    TS_LUA_HOOK_SEND_REQUEST_HDR
    TS_LUA_HOOK_READ_RESPONSE_HDR
    TS_LUA_HOOK_SEND_RESPONSE_HDR
    TS_LUA_REQUEST_TRANSFORM
    TS_LUA_RESPONSE_TRANSFORM
    
These constants are usually used in ts.hook method call.


ts.ctx
------
**syntax**: *ts.ctx[KEY]*

**context**: do_remap or later

**description**: This table can be used to store per-request Lua context data and has a life time identical to the current request.

Here is an example:

    function send_response()
        ts.client_response.header['RR'] = ts.ctx['rhost']
        return 0
    end
    
    function do_remap()
        local req_host = ts.client_request.header.Host
        ts.ctx['rhost'] = string.reverse(req_host)
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end


ts.http.get_cache_lookup_status
------
**syntax**: *ts.http.get_cache_lookup_status()*

**context**: function @ TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE hook point

**description**: This function can be used to get cache lookup status.

Here is an example:

    function send_response()
        ts.client_response.header['CStatus'] = ts.ctx['cstatus']
    end
    
    function cache_lookup()
        local cache_status = ts.http.get_cache_lookup_status()
        if cache_status == TS_LUA_CACHE_LOOKUP_HIT_FRESH:
            ts.ctx['cstatus'] = 'hit'
        else
            ts.ctx['cstatus'] = 'not hit'
        end
    end
    
    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end


Http cache lookup status constants
------
**context**: global

    TS_LUA_CACHE_LOOKUP_MISS (0)
    TS_LUA_CACHE_LOOKUP_HIT_STALE (1)
    TS_LUA_CACHE_LOOKUP_HIT_FRESH (2)
    TS_LUA_CACHE_LOOKUP_SKIPPED (3)


ts.http.set_cache_url
------
**syntax**: *ts.http.set_cache_url(KEY_URL)*

**context**: do_remap

**description**: This function can be used to modify the cache key for the request.

Here is an example:

    function do_remap()
        ts.http.set_cache_url('http://127.0.0.1:8080/abc/')
        return 0
    end


ts.http.resp_cache_transformed
------
**syntax**: *ts.http.resp_cache_transformed(BOOL)*

**context**: do_remap or later

**description**: This function can be used to tell trafficserver whether to cache the transformed data.

Here is an example:

    function upper_transform(data, eos)
        if eos == 1 then
            return string.upper(data)..'S.H.E.\n', eos
        else
            return string.upper(data), eos
        end
    end
    
    function do_remap()
        ts.hook(TS_LUA_RESPONSE_TRANSFORM, upper_transform)
        ts.http.resp_cache_transformed(0)
        ts.http.resp_cache_untransformed(1)
        return 0
    end
    
This function is usually called after we hook TS_LUA_RESPONSE_TRANSFORM.


ts.http.resp_cache_untransformed
------
**syntax**: *ts.http.resp_cache_untransformed(BOOL)*

**context**: do_remap or later

**description**: This function can be used to tell trafficserver whether to cache the untransformed data.

Here is an example:

    function upper_transform(data, eos)
        if eos == 1 then
            return string.upper(data)..'S.H.E.\n', eos
        else
            return string.upper(data), eos
        end
    end
    
    function do_remap()
        ts.hook(TS_LUA_RESPONSE_TRANSFORM, upper_transform)
        ts.http.resp_cache_transformed(0)
        ts.http.resp_cache_untransformed(1)
        return 0
    end
    
This function is usually called after we hook TS_LUA_RESPONSE_TRANSFORM.


ts.client_request.client_addr.get_addr
------
**syntax**: *ts.client_request.client_addr.get_addr()*

**context**: do_remap or later

**description**: This function can be used to get socket address of the client.

Here is an example:

    function do_remap
        ip, port, family = ts.client_request.client_addr.get_addr()
        return 0
    end

The ts.client_request.client_addr.get_addr function returns three values, ip is a string, port and family is number.


ts.client_request.get_method
------
**syntax**: *ts.client_request.get_method()*

**context**: do_remap or later

**description**: This function can be used to retrieve the current request's request method name. String like "GET" or 
"POST" is returned.


ts.client_request.set_method
------
**syntax**: *ts.client_request.set_method(METHOD_NAME)*

**context**: do_remap

**description**: This function can be used to override the current request's request method with METHOD_NAME.


ts.client_request.get_url
------
**syntax**: *ts.client_request.get_url()*

**context**: do_remap or later

**description**: This function can be used to retrieve the whole request's url.


ts.client_request.get_uri
------
**syntax**: *ts.client_request.get_uri()*

**context**: do_remap or later

**description**: This function can be used to retrieve the request's path.


ts.client_request.set_uri
------
**syntax**: *ts.client_request.set_uri(PATH)*

**context**: do_remap

**description**: This function can be used to override the request's path.


ts.client_request.get_uri_args
------
**syntax**: *ts.client_request.get_uri_args()*

**context**: do_remap or later

**description**: This function can be used to retrieve the request's query string.


ts.client_request.set_uri_args
------
**syntax**: *ts.client_request.set_uri_args(QUERY_STRING)*

**context**: do_remap

**description**: This function can be used to override the request's query string.


ts.client_request.header.HEADER
------
**syntax**: *ts.client_request.header.HEADER = VALUE*

**syntax**: *ts.client_request.header[HEADER] = VALUE*

**syntax**: *VALUE = ts.client_request.header.HEADER*

**context**: do_remap or later

**description**: Set, add to, clear or get the current request's HEADER.

Here is an example:

    function do_remap()
        local req_host = ts.client_request.header.Host
        ts.client_request.header['Host'] = 'a.tbcdn.cn'
    end


TODO
=======
Short Term
------
* non-blocking I/O operation
* ts.fetch

Long Term
------
* ts.regex

