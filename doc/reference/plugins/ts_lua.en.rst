.. _ts-lua-plugin:

ts-lua Plugin
*************

.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at
 
   http://www.apache.org/licenses/LICENSE-2.0
 
  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.


This module embeds Lua, via the standard Lua 5.1 interpreter, into Apache Traffic Server. With this module, we can
implement ATS plugin by writing lua script instead of c code. Lua code executed using this module can be 100%
non-blocking because the powerful Lua coroutines had been integrated in to ATS event model.

Synopsis
========
**test_hdr.lua**

::

    function send_response()
        ts.client_response.header['Rhost'] = ts.ctx['rhost']
        return 0
    end

    function do_remap()
        local req_host = ts.client_request.header.Host
        ts.ctx['rhost'] = string.reverse(req_host)
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end

**sethost.lua**

::

    local HOSTNAME = ''
    function __init__(argtb)
        if (#argtb) < 1 then
            print(argtb[0], 'hostname parameter required!!')
            return -1
        end
        HOSTNAME = argtb[1]
    end

    function do_remap()
        ts.client_request.header['Host'] = HOSTNAME
        return 0
    end


Installation
============

This plugin is only built if the configure option

::

    --enable-experimental-plugins

is given at build time.

Configuration
=============

This module acts as remap plugin of Traffic Server, so we should realize 'do_remap' function in each lua script. We can
write this in remap.config:

::

    map http://a.tbcdn.cn/ http://inner.tbcdn.cn/ @plugin=/XXX/tslua.so @pparam=/XXX/test_hdr.lua

Sometimes we want to receive parameters and process them in the script, we should realize '__init__' function in the lua
script, and we can write this in remap.config:

::

    map http://a.x.cn/ http://b.x.cn/ @plugin=/X/tslua.so @pparam=/X/sethost.lua @pparam=a.st.cn

This module can also act as a global plugin of Traffic Server. In this case we should provide one of these functions in
each lua script:

- **'do_global_txn_start'**
- **'do_global_txn_close'**
- **'do_global_os_dns'**
- **'do_global_pre_remap'**
- **'do_global_post_remap'**
- **'do_global_read_request'**
- **'do_global_send_request'**
- **'do_global_read_response'**
- **'do_global_send_response'**
- **'do_global_cache_lookup_complete'**
- **'do_global_read_cache'**
- **'do_global_select_alt'**

We can write this in plugin.config:

::
  
    tslua.so /etc/trafficserver/script/test_global_hdr.lua


TS API for Lua
==============

Introduction
------------
The API is exposed to Lua in the form of one standard packages ``ts``. This package is in the default global scope and
is always available within lua script. This package can be introduced into Lua like this:

::

    ts.say('Hello World')
    ts.sleep(10)

`TOP <#ts-lua-plugin>`_

ts.now
------
**syntax:** *val = ts.now()*

**context:** global

**description:** This function returns the time since the Epoch (00:00:00 UTC, January 1, 1970), measured in seconds.

Here is an example:

::

    local nt = ts.now()  -- 1395221053

`TOP <#ts-lua-plugin>`_

ts.debug
--------
**syntax:** *ts.debug(MESSAGE)*

**context:** global

**description**: Log the MESSAGE to traffic.out if debug is enabled.

Here is an example:

::

       ts.debug('I am in do_remap now.')

The debug tag is **ts_lua** and we should write this in records.config:

``CONFIG proxy.config.diags.debug.tags STRING ts_lua``

`TOP <#ts-lua-plugin>`_

Remap status constants
----------------------
**context:** do_remap

::

    TS_LUA_REMAP_NO_REMAP (0)
    TS_LUA_REMAP_DID_REMAP (1)
    TS_LUA_REMAP_NO_REMAP_STOP (2)
    TS_LUA_REMAP_DID_REMAP_STOP (3)
    TS_LUA_REMAP_ERROR (-1)

These constants are usually used as return value of do_remap function.

`TOP <#ts-lua-plugin>`_

ts.hook
-------
**syntax:** *ts.hook(HOOK_POINT, FUNCTION)*

**context:** global or do_remap or do_global_* or later

**description**: Hooks are points in http transaction processing where we can step in and do some work. FUNCTION will be
called when the http transaction steps in to HOOK_POINT.

Here is an example

::

    function send_response()
        s.client_response.header['SHE'] = 'belief'
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
    end

Then the client will get the response like this:

::

    HTTP/1.1 200 OK
    Content-Type: text/html
    Server: ATS/3.2.0
    SHE: belief
    Connection: Keep-Alive
    ...

You can create global hook as well

::

    function do_some_work()
       ts.debug('do_some_work')
       return 0  
    end

    ts.hook(TS_LUA_HOOK_READ_REQUEST_HDR, do_some_work)

Or you can do it this way

    ts.hook(TS_LUA_HOOK_READ_REQUEST_HDR, 
        function()
            ts.debug('do_some_work')
            return 0
        end
    )

Also the return value of the function will control how the transaction will be re-enabled. Return value of 0 will cause
the transaction to be re-enabled normally (TS_EVENT_HTTP_CONTINUE). Return value of 1 will be using TS_EVENT_HTTP_ERROR
instead.

`TOP <#ts-lua-plugin>`_

Hook point constants
--------------------
**context:** do_remap or later

::

    TS_LUA_HOOK_OS_DNS
    TS_LUA_HOOK_PRE_REMAP
    TS_LUA_HOOK_READ_CACHE_HDR
    TS_LUA_HOOK_SELECT_ALT
    TS_LUA_HOOK_TXN_CLOSE
    TS_LUA_HOOK_POST_REMAP
    TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE
    TS_LUA_HOOK_READ_REQUEST_HDR
    TS_LUA_HOOK_SEND_REQUEST_HDR
    TS_LUA_HOOK_READ_RESPONSE_HDR
    TS_LUA_HOOK_SEND_RESPONSE_HDR
    TS_LUA_REQUEST_TRANSFORM
    TS_LUA_RESPONSE_TRANSFORM

These constants are usually used in ts.hook method call.

`TOP <#ts-lua-plugin>`_

ts.ctx
------
**syntax:** *ts.ctx[KEY] = VALUE*

**syntax:** *VALUE = ts.ctx[KEY]*

**context:** do_remap or do_global_* or later

**description:** This table can be used to store per-request Lua context data and has a life time identical to the
current request.

Here is an example:

::

    function send_response()
        ts.client_response.header['F-Header'] = ts.ctx['hdr']
        return 0
    end

    function do_remap()
        ts.ctx['hdr'] = 'foo'
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end

Then the client will get the response like this:

::

    HTTP/1.1 200 OK
    Content-Type: text/html
    Server: ATS/3.2.0
    F-Header: foo
    Connection: Keep-Alive
    ...

`TOP <#ts-lua-plugin>`_

ts.client_request.get_method
----------------------------
**syntax:** *ts.client_request.get_method()*

**context:** do_remap or do_global_* or later

**description:** This function can be used to retrieve the current client request's method name. String like "GET" or
"POST" is returned.

`TOP <#ts-lua-plugin>`_

ts.client_request.set_method
----------------------------
**syntax:** *ts.client_request.set_method()*

**context:** do_remap or do_global_*

**description:** This function can be used to override the current client request's method with METHOD_NAME.

ts.client_request.get_version
-----------------------------
**syntax:** *ver = ts.client_request.get_version()*

**context:** do_remap or do_global_* or later

**description:** Return the http version string of the client request.

Current possible values are 1.0, 1.1, and 0.9.

`TOP <#ts-lua-plugin>`_

ts.client_request.set_version
-----------------------------
**syntax:** *ts.client_request.set_version(VERSION_STR)*

**context:** do_remap or do_global_* or later

**description:** Set the http version of the client request with the VERSION_STR

::

    ts.client_request.set_version('1.0')

`TOP <#ts-lua-plugin>`_

ts.client_request.get_uri
-------------------------
**syntax:** *ts.client_request.get_uri()*

**context:** do_remap or later

**description:** This function can be used to retrieve the client request's path.

Here is an example:

::

    function do_remap()
        local uri = ts.client_request.get_uri()
        print(uri)
    end

Then ``GET /st?a=1`` will yield the output:

``/st``


`TOP <#ts-lua-plugin>`_

ts.client_request.set_uri
-------------------------
**syntax:** *ts.client_request.set_uri(PATH)*

**context:** do_remap or do_global_* 

**description:** This function can be used to override the client request's path.

The PATH argument must be a Lua string and starts with ``/``


`TOP <#ts-lua-plugin>`_

ts.client_request.get_uri_args
------------------------------
**syntax:** *ts.client_request.get_uri_args()*

**context:** do_remap or do_global_* or later

**description:** This function can be used to retrieve the client request's query string.

Here is an example:

::

    function do_remap()
        local query = ts.client_request.get_uri_args()
        print(query)
    end

Then ``GET /st?a=1&b=2`` will yield the output:

``a=1&b=2``


`TOP <#ts-lua-plugin>`_

ts.client_request.set_uri_args
------------------------------
**syntax:** *ts.client_request.set_uri_args(QUERY_STRING)*

**context:** do_remap or do_global_* 

**description:** This function can be used to override the client request's query string.

::

    ts.client_request.set_uri_args('n=6&p=7')


`TOP <#ts-lua-plugin>`_

ts.client_request.get_url
-------------------------
**syntax:** *ts.client_request.get_url()*

**context:** do_remap or do_global_* or later

**description:** This function can be used to retrieve the whole client request's url.

Here is an example:

::

    function do_remap()
        local url = ts.client_request.get_url()
        print(url)
    end

Then ``GET /st?a=1&b=2 HTTP/1.1\r\nHost: a.tbcdn.cn\r\n...`` will yield the output:

``http://a.tbcdn.cn/st?a=1&b=2``

`TOP <#ts-lua-plugin>`_

ts.client_request.header.HEADER
-------------------------------
**syntax:** *ts.client_request.header.HEADER = VALUE*

**syntax:** *ts.client_request.header[HEADER] = VALUE*

**syntax:** *VALUE = ts.client_request.header.HEADER*

**context:** do_remap or do_global_* or later

**description:** Set, add to, clear or get the current client request's HEADER.

Here is an example:

::

    function do_remap()
        local ua = ts.client_request.header['User-Agent']
        print(ua)
        ts.client_request.header['Host'] = 'a.tbcdn.cn'
    end

Then ``GET /st HTTP/1.1\r\nHost: b.tb.cn\r\nUser-Agent: Mozilla/5.0\r\n...`` will yield the output:

``Mozilla/5.0``


`TOP <#ts-lua-plugin>`_

ts.client_request.get_headers
-----------------------------
**syntax:** *ts.client_request.get_headers()*

**context:** do_remap or do_global_* or later

**description:** Returns a Lua table holding all the headers for the current client request.

Here is an example:

::

    function do_remap()
        hdrs = ts.client_request.get_headers()
        for k, v in pairs(hdrs) do
            print(k..': '..v)
        end
    end

Then ``GET /st HTTP/1.1\r\nHost: b.tb.cn\r\nUser-Aget: Mozilla/5.0\r\nAccept: */*`` will yield the output ::

    Host: b.tb.cn
    User-Agent: Mozilla/5.0
    Accept: */*


`TOP <#ts-lua-plugin>`_

ts.client_request.client_addr.get_addr
--------------------------------------
**syntax:** *ts.client_request.client_addr.get_addr()*

**context:** do_remap or do_global_* or later

**description**: This function can be used to get socket address of the client.

The ts.client_request.client_addr.get_addr function returns three values, ip is a string, port and family is number. 

Here is an example:

::

    function do_remap()
        ip, port, family = ts.client_request.client_addr.get_addr()
        print(ip)               -- 192.168.231.17
        print(port)             -- 17786
        print(family)           -- 2(AF_INET)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.client_request.get_url_host
------------------------------
**syntax:** *host = ts.client_request.get_url_host()*

**context:** do_remap or do_global_* or later

**description:** Return the ``host`` field of the request url.

Here is an example:

::

    function do_remap()
        local url_host = ts.client_request.get_url_host()
        print(url_host)
    end

Then ``GET /liuyurou.txt HTTP/1.1\r\nHost: 192.168.231.129:8080\r\n...`` will yield the output:

``192.168.231.129``

`TOP <#ts-lua-plugin>`_

ts.client_request.set_url_host
------------------------------
**syntax:** *ts.client_request.set_url_host(str)*

**context:** do_remap or do_global_* 

**description:** Set ``host`` field of the request url with ``str``. This function is used to change the address of the
origin server, and we should return TS_LUA_REMAP_DID_REMAP(_STOP) in do_remap.

Here is an example:

::

    function do_remap()
        ts.client_request.set_url_host('192.168.231.130')
        ts.client_request.set_url_port(80)
        ts.client_request.set_url_scheme('http')
        return TS_LUA_REMAP_DID_REMAP
    end

remap.config like this:

::

    map http://192.168.231.129:8080/ http://192.168.231.129:9999/

Then server request will connect to ``192.168.231.130:80``

`TOP <#ts-lua-plugin>`_

ts.client_request.get_url_port
------------------------------
**syntax:** *port = ts.client_request.get_url_port()*

**context:** do_remap or do_global_* or later

**description:** Returns the ``port`` field of the request url as a Lua number.

Here is an example:

::

    function do_remap()
        local url_port = ts.client_request.get_url_port()
        print(url_port)
    end

Then Then ``GET /liuyurou.txt HTTP/1.1\r\nHost: 192.168.231.129:8080\r\n...`` will yield the output:

``8080``


`TOP <#ts-lua-plugin>`_

ts.client_request.set_url_port
------------------------------
**syntax:** *ts.client_request.set_url_port(NUMBER)*

**context:** do_remap or do_global_*

**description:** Set ``port`` field of the request url with ``NUMBER``. This function is used to change the address of
the origin server, and we should return TS_LUA_REMAP_DID_REMAP(_STOP) in do_remap.


`TOP <#ts-lua-plugin>`_

ts.client_request.get_url_scheme
--------------------------------
**syntax:** *scheme = ts.client_request.get_url_scheme()*

**context:** do_remap or do_global_* or later

**description:** Return the ``scheme`` field of the request url.

Here is an example:

::

    function do_remap()
        local url_scheme = ts.client_request.get_url_scheme()
        print(url_scheme)
    end

Then ``GET /liuyurou.txt HTTP/1.1\r\nHost: 192.168.231.129:8080\r\n...`` will yield the output:

``http``


`TOP <#ts-lua-plugin>`_

ts.client_request.set_url_scheme
--------------------------------
**syntax:** *ts.client_request.set_url_scheme(str)*

**context:** do_remap or do_global_* 

**description:** Set ``scheme`` field of the request url with ``str``. This function is used to change the scheme of the
server request, and we should return TS_LUA_REMAP_DID_REMAP(_STOP) in do_remap.


`TOP <#ts-lua-plugin>`_

ts.http.set_cache_url
---------------------
**syntax:** *ts.http.set_cache_url(KEY_URL)*

**context:** do_remap or do_global_* 

**description:** This function can be used to modify the cache key for the client request.

Here is an example:

::

    function do_remap()
        ts.http.set_cache_url('http://a.tbcdn.cn/foo')
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.set_resp
----------------
**syntax:** *ts.http.set_resp(CODE, BODY)*

**context:** do_remap or do_global_*

**description:** This function can be used to set response for the client with the CODE status and BODY string.

Here is an example:

::

    function do_remap()
        ts.http.set_resp(403, "Document access failed :)\n")
        return 0
    end

We will get the response like this:

::

    HTTP/1.1 403 Forbidden
    Date: Thu, 20 Mar 2014 06:12:43 GMT
    Connection: close
    Server: ATS/5.0.0
    Cache-Control: no-store
    Content-Type: text/html
    Content-Language: en
    Content-Length: 27

    Document access failed :)


`TOP <#ts-lua-plugin>`_

ts.http.get_cache_lookup_status
-------------------------------
**syntax:** *ts.http.get_cache_lookup_status()*

**context:** function @ TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE hook point

**description:** This function can be used to get cache lookup status.

Here is an example:

::

    function cache_lookup()
        local cache_status = ts.http.get_cache_lookup_status()
        if cache_status == TS_LUA_CACHE_LOOKUP_HIT_FRESH then
            print('hit')
        else
            print('not hit')
        end
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        return 0
    end


`TOP <#ts-lua-plugin>`_

Http cache lookup status constants
----------------------------------
**context:** global

::

    TS_LUA_CACHE_LOOKUP_MISS (0)
    TS_LUA_CACHE_LOOKUP_HIT_STALE (1)
    TS_LUA_CACHE_LOOKUP_HIT_FRESH (2)
    TS_LUA_CACHE_LOOKUP_SKIPPED (3)


`TOP <#ts-lua-plugin>`_

ts.cached_response.get_status
-----------------------------
**syntax:** *status = ts.cached_response.get_status()*

**context:** function @ TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE hook point or later

**description:** This function can be used to retrieve the status code of the cached response. A Lua number will be
returned.

Here is an example:

::

    function cache_lookup()
        local cache_status = ts.http.get_cache_lookup_status()
        if cache_status == TS_LUA_CACHE_LOOKUP_HIT_FRESH then
            code = ts.cached_response.get_status()
            print(code)         -- 200
        end
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.cached_response.get_version
------------------------------
**syntax:** *ver = ts.cached_response.get_version()*

**context:** function @ TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE hook point or later

**description:** Return the http version string of the cached response.

Current possible values are 1.0, 1.1, and 0.9.


`TOP <#ts-lua-plugin>`_

ts.cached_response.header.HEADER
--------------------------------
**syntax:** *VALUE = ts.cached_response.header.HEADER*

**syntax:** *VALUE = ts.cached_response.header[HEADER]*

**context:** function @ TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE hook point or later

**description:** get the current cached response's HEADER.

Here is an example:

::

    function cache_lookup()
        local status = ts.http.get_cache_lookup_status()
        if status == TS_LUA_CACHE_LOOKUP_HIT_FRESH then
            local ct = ts.cached_response.header['Content-Type']
            print(ct)         -- text/plain
        end
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        return 0
    end


`TOP <#ts-lua-plugin>`_

ts.cached_response.get_headers
------------------------------
**syntax:** *ts.cached_response.get_headers()*

**context:** function @ TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE hook point or later

**description:** Returns a Lua table holding all the headers for the current cached response.

Here is an example:

::

    function cache_lookup()
        local status = ts.http.get_cache_lookup_status()
        if status == TS_LUA_CACHE_LOOKUP_HIT_FRESH then
            hdrs = ts.cached_response.get_headers()
            for k, v in pairs(hdrs) do
                print(k..': '..v)
            end
        end
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        return 0
    end

We will get the output:

::

    Connection: keep-alive
    Content-Type: text/plain
    Date: Thu, 20 Mar 2014 06:12:20 GMT
    Cache-Control: max-age=864000
    Last-Modified: Sun, 19 May 2013 13:22:01 GMT
    Accept-Ranges: bytes
    Content-Length: 15
    Server: ATS/5.0.0


`TOP <#ts-lua-plugin>`_


ts.server_request.get_uri
-------------------------
**syntax:** *ts.server_request.get_uri()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description:** This function can be used to retrieve the server request's path.

Here is an example:

::

    function send_request()
        local uri = ts.server_request.get_uri()
        print(uri)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
        return 0
    end

Then ``GET /am.txt?a=1`` will yield the output:

``/am.txt``


`TOP <#ts-lua-plugin>`_

ts.server_request.set_uri
-------------------------
**syntax:** *ts.server_request.set_uri(PATH)*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point

**description:** This function can be used to override the server request's path.

The PATH argument must be a Lua string and starts with ``/``


`TOP <#ts-lua-plugin>`_

ts.server_request.get_uri_args
------------------------------
**syntax:** *ts.server_request.get_uri_args()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description:** This function can be used to retrieve the server request's query string.

Here is an example:

::

    function send_request()
        local query = ts.server_request.get_uri_args()
        print(query)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
        return 0
    end

Then ``GET /st?a=1&b=2`` will yield the output:

``a=1&b=2``


`TOP <#ts-lua-plugin>`_

ts.server_request.set_uri_args
------------------------------
**syntax:** *ts.server_request.set_uri_args(QUERY_STRING)*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point

**description:** This function can be used to override the server request's query string.

::

    ts.server_request.set_uri_args('n=6&p=7')


`TOP <#ts-lua-plugin>`_

ts.server_request.header.HEADER
-------------------------------
**syntax:** *ts.server_request.header.HEADER = VALUE*

**syntax:** *ts.server_request.header[HEADER] = VALUE*

**syntax:** *VALUE = ts.server_request.header.HEADER*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description:** Set, add to, clear or get the current server request's HEADER.

Here is an example:

::

    function send_request()
        local ua = ts.server_request.header['User-Agent']
        print(ua)
        ts.server_request.header['Accept-Encoding'] = 'gzip'
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
        return 0
    end

Then ``GET /st HTTP/1.1\r\nHost: b.tb.cn\r\nUser-Agent: Mozilla/5.0\r\n...`` will yield the output:

``Mozilla/5.0``


`TOP <#ts-lua-plugin>`_

ts.server_request.get_headers
-----------------------------
**syntax:** *ts.server_request.get_headers()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description:** Returns a Lua table holding all the headers for the current server request.

Here is an example:

::

    function send_request()
        hdrs = ts.cached_response.get_headers()
        for k, v in pairs(hdrs) do
            print(k..': '..v)
        end
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
        return 0
    end

We will get the output:

::

    Host: b.tb.cn
    User-Agent: curl/7.19.7
    Accept: */*


`TOP <#ts-lua-plugin>`_

ts.server_response.get_status
-----------------------------
**syntax:** *status = ts.server_response.get_status()*

**context:** function @ TS_LUA_HOOK_READ_RESPONSE_HDR hook point or later

**description:** This function can be used to retrieve the status code of the origin server's response. A Lua number
will be returned.

Here is an example:

::

    function read_response()
        local code = ts.server_response.get_status()
        print(code)         -- 200
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_READ_RESPONSE_HDR, read_response)
        return 0
    end


`TOP <#ts-lua-plugin>`_'

ts.server_response.set_status
-----------------------------
**syntax:** *ts.server_response.set_status(NUMBER)*

**context:** function @ TS_LUA_HOOK_READ_RESPONSE_HDR hook point

**description:** This function can be used to set the status code of the origin server's response.

Here is an example:

::

    function read_response()
        ts.server_response.set_status(404)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_READ_RESPONSE_HDR, read_response)
        return 0
    end


`TOP <#ts-lua-plugin>`_'

ts.server_response.get_version
------------------------------
**syntax:** *ver = ts.server_response.get_version()*

**context:** function @ TS_LUA_HOOK_READ_RESPONSE_HDR hook point or later.

**description:** Return the http version string of the server response.

Current possible values are 1.0, 1.1, and 0.9.

`TOP <#ts-lua-plugin>`_

ts.server_response.set_version
------------------------------
**syntax:** *ts.server_response.set_version(VERSION_STR)*

**context:** function @ TS_LUA_HOOK_READ_RESPONSE_HDR hook point

**description:** Set the http version of the server response with the VERSION_STR

::

    ts.server_response.set_version('1.0')

`TOP <#ts-lua-plugin>`_

ts.server_response.header.HEADER
--------------------------------
**syntax:** *ts.server_response.header.HEADER = VALUE*

**syntax:** *ts.server_response.header[HEADER] = VALUE*

**syntax:** *VALUE = ts.server_response.header.HEADER*

**context:** function @ TS_LUA_HOOK_READ_RESPONSE_HDR hook point or later.

**description:** Set, add to, clear or get the current server response's HEADER.

Here is an example:

::

    function read_response()
        local ct = ts.server_response.header['Content-Type']
        print(ct)
        ts.server_response.header['Cache-Control'] = 'max-age=14400'
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_READ_RESPONSE_HDR, read_response)
        return 0
    end

We will get the output:

``text/html``


`TOP <#ts-lua-plugin>`_'

ts.server_response.get_headers
------------------------------
**syntax:** *ts.server_response.get_headers()*

**context:** function @ TS_LUA_HOOK_READ_RESPONSE_HDR hook point or later

**description:** Returns a Lua table holding all the headers for the current server response.

Here is an example:

::

    function read_response()
        hdrs = ts.server_response.get_headers()
        for k, v in pairs(hdrs) do
            print(k..': '..v)
        end
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_READ_RESPONSE_HDR, read_response)
        return 0
    end

We will get the output:

::

    Server: nginx/1.5.9
    Date: Tue, 18 Mar 2014 10:12:25 GMT
    Content-Type: text/html
    Content-Length: 555
    Last-Modified: Mon, 19 Aug 2013 14:25:55 GMT
    Connection: keep-alive
    ETag: "52122af3-22b"
    Cache-Control: max-age=14400
    Accept-Ranges: bytes


`TOP <#ts-lua-plugin>`_

ts.client_response.get_status
-----------------------------
**syntax:** *status = ts.client_response.get_status()*

**context:** function @ TS_LUA_HOOK_SEND_RESPONSE_HDR hook point

**description:** This function can be used to retrieve the status code of the response to the client. A Lua number will
be returned.

Here is an example:

::

    function send_response()
        local code = ts.client_response.get_status()
        print(code)         -- 200
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end


`TOP <#ts-lua-plugin>`_

ts.client_response.set_status
-----------------------------
**syntax:** *ts.client_response.set_status(NUMBER)*

**context:** function @ TS_LUA_HOOK_SEND_RESPONSE_HDR hook point

**description:** This function can be used to set the status code of the response to the client.

Here is an example:

::

    function send_response()
        ts.client_response.set_status(404)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end


`TOP <#ts-lua-plugin>`_

ts.client_response.get_version
------------------------------
**syntax:** *ver = ts.client_response.get_version()*

**context:** function @ TS_LUA_HOOK_SEND_RESPONSE_HDR hook point.

**description:** Return the http version string of the response to the client.

Current possible values are 1.0, 1.1, and 0.9.

`TOP <#ts-lua-plugin>`_

ts.client_response.set_version
------------------------------
**syntax:** *ts.client_response.set_version(VERSION_STR)*

**context:** function @ TS_LUA_HOOK_SEND_RESPONSE_HDR hook point

**description:** Set the http version of the response to the client with the VERSION_STR

::

    ts.client_response.set_version('1.0')

`TOP <#ts-lua-plugin>`_

ts.client_response.header.HEADER
--------------------------------
**syntax:** *ts.client_response.header.HEADER = VALUE*

**syntax:** *ts.client_response.header[HEADER] = VALUE*

**syntax:** *VALUE = ts.client_response.header.HEADER*

**context:** function @ TS_LUA_HOOK_SEND_RESPONSE_HDR hook point.

**description:** Set, add to, clear or get the current client response's HEADER.

Here is an example:

::

    function send_response()
        local ct = ts.client_response.header['Content-Type']
        print(ct)
        ts.client_response.header['Cache-Control'] = 'max-age=3600'
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end

We will get the output:

``text/html``


`TOP <#ts-lua-plugin>`_

ts.client_response.get_headers
------------------------------
**syntax:** *ts.client_response.get_headers()*

**context:** function @ TS_LUA_HOOK_SEND_RESPONSE_HDR hook point.

**description:** Returns a Lua table holding all the headers for the current client response.

Here is an example:

::

    function send_response()
        hdrs = ts.client_response.get_headers()
        for k, v in pairs(hdrs) do
            print(k..': '..v)
        end
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end

We will get the output:

::

    Server: ATS/5.0.0
    Date: Tue, 18 Mar 2014 10:12:25 GMT
    Content-Type: text/html
    Transfer-Encoding: chunked
    Last-Modified: Mon, 19 Aug 2013 14:25:55 GMT
    Connection: keep-alive
    Cache-Control: max-age=14400
    Age: 2641
    Accept-Ranges: bytes


`TOP <#ts-lua-plugin>`_

ts.client_response.set_error_resp
---------------------------------
**syntax:** *ts.client_response.set_error_resp(CODE, BODY)*

**context:** function @ TS_LUA_HOOK_SEND_RESPONSE_HDR hook point.

**description:** This function can be used to set the error response to the client.

With this function we can jump to send error response to the client if exception exists, meanwhile we should return `-1`
from the function where exception raises.

Here is an example:

::

    function send_response()
        ts.client_response.set_error_resp(404, 'bad luck :(\n')
    end

    function cache_lookup()
        return -1
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
        return 0
    end

We will get the response like this:

::

    HTTP/1.1 404 Not Found
    Date: Tue, 18 Mar 2014 11:16:00 GMT
    Connection: keep-alive
    Server: ATS/5.0.0
    Content-Length: 12

    bad luck :(


`TOP <#ts-lua-plugin>`_

ts.http.resp_cache_transformed
------------------------------
**syntax:** *ts.http.resp_cache_transformed(BOOL)*

**context:** do_remap or do_global_* or later

**description**: This function can be used to tell trafficserver whether to cache the transformed data.

Here is an example:

::

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
        return 0
    end

This function is usually called after we hook TS_LUA_RESPONSE_TRANSFORM.


`TOP <#ts-lua-plugin>`_

ts.http.resp_cache_untransformed
--------------------------------
**syntax:** *ts.http.resp_cache_untransformed(BOOL)*

**context:** do_remap or do_global_* or later

**description**: This function can be used to tell trafficserver whether to cache the untransformed data.

Here is an example:

::

    function upper_transform(data, eos)
        if eos == 1 then
            return string.upper(data)..'S.H.E.\n', eos
        else
            return string.upper(data), eos
        end
    end

    function do_remap()
        ts.hook(TS_LUA_RESPONSE_TRANSFORM, upper_transform)
        ts.http.resp_cache_untransformed(1)
        return 0
    end

This function is usually called after we hook TS_LUA_RESPONSE_TRANSFORM.


`TOP <#ts-lua-plugin>`_

ts.http.skip_remapping_set
-------------------------
**syntax:** *ts.http.skip_remapping_set(BOOL)*

**context:** do_global_read_request

**description**: This function can be used to tell trafficserver to skip doing remapping

Here is an example:

::

    function do_global_read_request()
        ts.http.skip_remapping_set(1);
        ts.client_request.header['Host'] = 'www.yahoo.com'
        return 0
    end

This function is usually called in do_global_read_request function

ts.http.is_internal_request
---------------------------
**syntax:** *ts.http.is_internal_request()*

** context:** do_remap or do_global_* or later

** description**: This function can be used to tell is a request is internal or not

Here is an example:

::

    function do_global_read_request()
        local internal = ts.http.is_internal_request()
        ts.debug(internal)
        return 0
    end

ts.add_package_path
-------------------
**syntax:** *ts.add_package_path(lua-style-path-str)*

**context:** init stage of the lua script

**description:** Adds the Lua module search path used by scripts.

The path string is in standard Lua path form.

Here is an example:

::

    ts.add_package_path('/home/a/test/lua/pac/?.lua')
    local nt = require("nt")
    function do_remap()
        print(nt.t9(7979))
        return 0
    end

`TOP <#ts-lua-plugin>`_


ts.add_package_cpath
--------------------
**syntax:** *ts.add_package_cpath(lua-style-cpath-str)*

**context:** init stage of the lua script

**description:** Adds the Lua C-module search path used by scripts.

The cpath string is in standard Lua cpath form.

Here is an example:

::

    ts.add_package_cpath('/home/a/test/c/module/?.so')
    local ma = require("ma")
    function do_remap()
        print(ma.ft())
        return 0
    end


`TOP <#ts-lua-plugin>`_


ts.md5
------
**syntax:** *digest = ts.md5(str)*

**context:** global

**description:** Returns the hexadecimal representation of the MD5 digest of the ``str`` argument.

Here is an example:

::

    function do_remap()
        uri = ts.client_request.get_uri()
        print(uri)              -- /foo
        print(ts.md5(uri))      -- 1effb2475fcfba4f9e8b8a1dbc8f3caf
    end


`TOP <#ts-lua-plugin>`_

ts.md5_bin
----------
**syntax:** *digest = ts.md5_bin(str)*

**context:** global

**description:** Returns the binary form of the MD5 digest of the ``str`` argument.

Here is an example:

::

    function do_remap()
        uri = ts.client_request.get_uri()
        bin = ts.md5_bin(uri)
    end


`TOP <#ts-lua-plugin>`_

ts.sha1
-------
**syntax:** *digest = ts.sha1(str)*

**context:** global

**description:** Returns the hexadecimal representation of the SHA-1 digest of the ``str`` argument.

Here is an example:

::

    function do_remap()
        uri = ts.client_request.get_uri()
        print(uri)              -- /foo
        print(ts.sha1(uri))     -- 6dbd548cc03e44b8b44b6e68e56255ce4273ae49
    end


`TOP <#ts-lua-plugin>`_

ts.sha1_bin
-----------
**syntax:** *digest = ts.sha1_bin(str)*

**context:** global

**description:** Returns the binary form of the SHA-1 digest of the ``str`` argument.

Here is an example:

::

    function do_remap()
        uri = ts.client_request.get_uri()
        bin = ts.sha1_bin(uri)
    end


`TOP <#ts-lua-plugin>`_

ts.intercept
------------
**syntax:** *ts.intercept(FUNCTION)*

**context:** do_remap or do_global_*

**description:** Intercepts the client request and processes it in FUNCTION.

We should construct the response for the client request, and the request will not be processed by other modules, like
hostdb, cache, origin server...

Here is an example:

::

    require 'os'

    function send_data()
        local nt = os.time()..' Zheng.\n'
        local resp =  'HTTP/1.0 200 OK\r\n' ..
                      'Server: ATS/3.2.0\r\n' ..
                      'Content-Type: text/plain\r\n' ..
                      'Content-Length: ' .. string.format('%d', string.len(nt)) .. '\r\n' ..
                      'Last-Modified: ' .. os.date("%a, %d %b %Y %H:%M:%S GMT", os.time()) .. '\r\n' ..
                      'Connection: keep-alive\r\n' ..
                      'Cache-Control: max-age=7200\r\n' ..
                      'Accept-Ranges: bytes\r\n\r\n' ..
                      nt
        ts.say(resp)
    end

    function do_remap()
        ts.http.intercept(send_data)
        return 0
    end

Then we will get the response like this:

::

    HTTP/1.1 200 OK
    Server: ATS/5.0.0
    Content-Type: text/plain
    Content-Length: 18
    Last-Modified: Tue, 18 Mar 2014 08:23:12 GMT
    Cache-Control: max-age=7200
    Accept-Ranges: bytes
    Date: Tue, 18 Mar 2014 12:23:12 GMT
    Age: 0
    Connection: keep-alive

    1395145392 Zheng.


`TOP <#ts-lua-plugin>`_

ts.say
------
**syntax:** *ts.say(data)*

**context:** *intercept or server_intercept*

**description:** Write response to ATS within intercept or server_intercept.


`TOP <#ts-lua-plugin>`_

ts.flush
--------
**syntax:** *ts.flush()*

**context:** *intercept or server_intercept*

**description:** Flushes the output to ATS within intercept or server_intercept.

In synchronous mode, the function will not return until all output data has been written into the system send buffer.
Note that using the Lua coroutine mechanism means that this function does not block the ATS event loop even in the
synchronous mode.

Here is an example:

::

    require 'os'

    function send_data()
        ss = 'wo ai yu ye hua\n'
        local resp =  'HTTP/1.0 200 OK\r\n' ..
                      'Server: ATS/3.2.0\r\n' ..
                      'Content-Type: text/plain\r\n' ..
                      'Content-Length: ' .. string.format('%d', 5*string.len(ss)) .. '\r\n' ..
                      'Last-Modified: ' .. os.date("%a, %d %b %Y %H:%M:%S GMT", os.time()) .. '\r\n' ..
                      'Connection: keep-alive\r\n' ..
                      'Cache-Control: max-age=7200\r\n' ..
                      'Accept-Ranges: bytes\r\n\r\n'
        ts.say(resp)
        for i=1, 5 do
            ts.say(ss)
            ts.flush()
        end
    end

    function do_remap()
        ts.http.intercept(send_data)
        return 0
    end

We will get the response like this:

::

    HTTP/1.1 200 OK
    Server: ATS/5.0.0
    Content-Type: text/plain
    Content-Length: 80
    Last-Modified: Tue, 18 Mar 2014 08:38:29 GMT
    Cache-Control: max-age=7200
    Accept-Ranges: bytes
    Date: Tue, 18 Mar 2014 12:38:29 GMT
    Age: 0
    Connection: keep-alive

    wo ai yu ye hua
    wo ai yu ye hua
    wo ai yu ye hua
    wo ai yu ye hua
    wo ai yu ye hua

`TOP <#ts-lua-plugin>`_

ts.sleep
--------
**syntax:** *ts.sleep(sec)*

**context:** *intercept or server_intercept*

**description:** Sleeps for the specified seconds without blocking.

Behind the scene, this method makes use of the ATS event model.

Here is an example:

::

    require 'os'

    function send_data()
        local nt = os.time()..' Zheng.\n'
        local resp =  'HTTP/1.0 200 OK\r\n' ..
                      'Server: ATS/3.2.0\r\n' ..
                      'Content-Type: text/plain\r\n' ..
                      'Content-Length: ' .. string.format('%d', string.len(nt)) .. '\r\n\r\n' ..
                      nt
        ts.sleep(3)
        ts.say(resp)
    end

    function do_remap()
        ts.http.intercept(send_data)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.server_intercept
-------------------
**syntax:** *ts.server_intercept(FUNCTION)*

**context:** do_remap or do_global_*

**description:** Intercepts the server request and acts as the origin server.

We should construct the response for the server request, so the request will be processed within FUNCTION in case of
miss for the cache lookup.

Here is an example:

::

    require 'os'

    function send_data()
        local nt = os.time()..' Zheng.\n'
        local resp =  'HTTP/1.0 200 OK\r\n' ..
                      'Server: ATS/3.2.0\r\n' ..
                      'Content-Type: text/plain\r\n' ..
                      'Content-Length: ' .. string.format('%d', string.len(nt)) .. '\r\n' ..
                      'Last-Modified: ' .. os.date("%a, %d %b %Y %H:%M:%S GMT", os.time()) .. '\r\n' ..
                      'Connection: keep-alive\r\n' ..
                      'Cache-Control: max-age=7200\r\n' ..
                      'Accept-Ranges: bytes\r\n\r\n' ..
                      nt
        ts.say(resp)
    end

    function do_remap()
        ts.http.server_intercept(send_data)
        return 0
    end

Then we will get the response like this:

::

    HTTP/1.1 200 OK
    Server: ATS/5.0.0
    Content-Type: text/plain
    Content-Length: 18
    Last-Modified: Tue, 18 Mar 2014 08:23:12 GMT
    Cache-Control: max-age=7200
    Accept-Ranges: bytes
    Date: Tue, 18 Mar 2014 12:23:12 GMT
    Age: 1890
    Connection: keep-alive

    1395145392 Zheng.


`TOP <#ts-lua-plugin>`_

ts.http.config_int_get
----------------------
**syntax:** *val = ts.http.config_int_get(CONFIG)*

**context:** do_remap or do_global_* or later.

**description:** Configuration option which has a int value can be retrieved with this function.

::

    val = ts.http.config_int_get(TS_LUA_CONFIG_HTTP_CACHE_HTTP)


`TOP <#ts-lua-plugin>`_

ts.http.config_int_set
----------------------
**syntax:** *ts.http.config_int_set(CONFIG, NUMBER)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to overwrite the configuration options.

Here is an example:

::

    function do_remap()
        ts.http.config_int_set(TS_LUA_CONFIG_HTTP_CACHE_HTTP, 0)    -- bypass the cache processor
        return 0
    end


`TOP <#ts-lua-plugin>`_

ts.http.config_float_get
------------------------
**syntax:** *val = ts.http.config_float_get(CONFIG)*

**context:** do_remap or do_global_* or later.

**description:** Configuration option which has a float value can be retrieved with this function.


`TOP <#ts-lua-plugin>`_

ts.http.config_float_set
------------------------
**syntax:** *ts.http.config_float_set(CONFIG, NUMBER)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to overwrite the configuration options.


`TOP <#ts-lua-plugin>`_

ts.http.config_string_get
-------------------------
**syntax:** *val = ts.http.config_string_get(CONFIG)*

**context:** do_remap or do_global_* or later.

**description:** Configuration option which has a string value can be retrieved with this function.


`TOP <#ts-lua-plugin>`_

ts.http.config_string_set
-------------------------
**syntax:** *ts.http.config_string_set(CONFIG, NUMBER)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to overwrite the configuration options.


`TOP <#ts-lua-plugin>`_

Http config constants
---------------------
**context:** do_remap or do_global_* or later

::

    TS_LUA_CONFIG_URL_REMAP_PRISTINE_HOST_HDR
    TS_LUA_CONFIG_HTTP_CHUNKING_ENABLED
    TS_LUA_CONFIG_HTTP_NEGATIVE_CACHING_ENABLED
    TS_LUA_CONFIG_HTTP_NEGATIVE_CACHING_LIFETIME
    TS_LUA_CONFIG_HTTP_CACHE_WHEN_TO_REVALIDATE
    TS_LUA_CONFIG_HTTP_KEEP_ALIVE_ENABLED_IN
    TS_LUA_CONFIG_HTTP_KEEP_ALIVE_ENABLED_OUT
    TS_LUA_CONFIG_HTTP_KEEP_ALIVE_POST_OUT
    TS_LUA_CONFIG_HTTP_SHARE_SERVER_SESSIONS
    TS_LUA_CONFIG_NET_SOCK_RECV_BUFFER_SIZE_OUT
    TS_LUA_CONFIG_NET_SOCK_SEND_BUFFER_SIZE_OUT
    TS_LUA_CONFIG_NET_SOCK_OPTION_FLAG_OUT
    TS_LUA_CONFIG_HTTP_FORWARD_PROXY_AUTH_TO_PARENT
    TS_LUA_CONFIG_HTTP_ANONYMIZE_REMOVE_FROM
    TS_LUA_CONFIG_HTTP_ANONYMIZE_REMOVE_REFERER
    TS_LUA_CONFIG_HTTP_ANONYMIZE_REMOVE_USER_AGENT
    TS_LUA_CONFIG_HTTP_ANONYMIZE_REMOVE_COOKIE
    TS_LUA_CONFIG_HTTP_ANONYMIZE_REMOVE_CLIENT_IP
    TS_LUA_CONFIG_HTTP_ANONYMIZE_INSERT_CLIENT_IP
    TS_LUA_CONFIG_HTTP_RESPONSE_SERVER_ENABLED
    TS_LUA_CONFIG_HTTP_INSERT_SQUID_X_FORWARDED_FOR
    TS_LUA_CONFIG_HTTP_SERVER_TCP_INIT_CWND
    TS_LUA_CONFIG_HTTP_SEND_HTTP11_REQUESTS
    TS_LUA_CONFIG_HTTP_CACHE_HTTP
    TS_LUA_CONFIG_HTTP_CACHE_CLUSTER_CACHE_LOCAL
    TS_LUA_CONFIG_HTTP_CACHE_IGNORE_CLIENT_NO_CACHE
    TS_LUA_CONFIG_HTTP_CACHE_IGNORE_CLIENT_CC_MAX_AGE
    TS_LUA_CONFIG_HTTP_CACHE_IMS_ON_CLIENT_NO_CACHE
    TS_LUA_CONFIG_HTTP_CACHE_IGNORE_SERVER_NO_CACHE
    TS_LUA_CONFIG_HTTP_CACHE_CACHE_RESPONSES_TO_COOKIES
    TS_LUA_CONFIG_HTTP_CACHE_IGNORE_AUTHENTICATION
    TS_LUA_CONFIG_HTTP_CACHE_CACHE_URLS_THAT_LOOK_DYNAMIC
    TS_LUA_CONFIG_HTTP_CACHE_REQUIRED_HEADERS
    TS_LUA_CONFIG_HTTP_INSERT_REQUEST_VIA_STR
    TS_LUA_CONFIG_HTTP_INSERT_RESPONSE_VIA_STR
    TS_LUA_CONFIG_HTTP_CACHE_HEURISTIC_MIN_LIFETIME
    TS_LUA_CONFIG_HTTP_CACHE_HEURISTIC_MAX_LIFETIME
    TS_LUA_CONFIG_HTTP_CACHE_GUARANTEED_MIN_LIFETIME
    TS_LUA_CONFIG_HTTP_CACHE_GUARANTEED_MAX_LIFETIME
    TS_LUA_CONFIG_HTTP_CACHE_MAX_STALE_AGE
    TS_LUA_CONFIG_HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_IN
    TS_LUA_CONFIG_HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_OUT
    TS_LUA_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_IN
    TS_LUA_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_OUT
    TS_LUA_CONFIG_HTTP_TRANSACTION_ACTIVE_TIMEOUT_OUT
    TS_LUA_CONFIG_HTTP_ORIGIN_MAX_CONNECTIONS
    TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES
    TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DEAD_SERVER
    TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_RR_RETRIES
    TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_TIMEOUT
    TS_LUA_CONFIG_HTTP_POST_CONNECT_ATTEMPTS_TIMEOUT
    TS_LUA_CONFIG_HTTP_DOWN_SERVER_CACHE_TIME
    TS_LUA_CONFIG_HTTP_DOWN_SERVER_ABORT_THRESHOLD
    TS_LUA_CONFIG_HTTP_CACHE_FUZZ_TIME
    TS_LUA_CONFIG_HTTP_CACHE_FUZZ_MIN_TIME
    TS_LUA_CONFIG_HTTP_DOC_IN_CACHE_SKIP_DNS
    TS_LUA_CONFIG_HTTP_RESPONSE_SERVER_STR
    TS_LUA_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR
    TS_LUA_CONFIG_HTTP_CACHE_FUZZ_PROBABILITY
    TS_LUA_CONFIG_NET_SOCK_PACKET_MARK_OUT
    TS_LUA_CONFIG_NET_SOCK_PACKET_TOS_OUT

`TOP <#ts-lua-plugin>`_

ts.http.cntl_get
----------------
**syntax:** *val = ts.http.cntl_get(CNTL_TYPE)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to retireve the value of control channel.

::

    val = ts.http.cntl_get(TS_LUA_HTTP_CNTL_GET_LOGGING_MODE)


`TOP <#ts-lua-plugin>`_

ts.http.cntl_set
----------------
**syntax:** *ts.http.cntl_set(CNTL_TYPE, BOOL)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to set the value of control channel.

Here is an example:

::

    function do_remap()
        ts.http.cntl_set(TS_LUA_HTTP_CNTL_SET_LOGGING_MODE, 0)      -- do not log the request
        return 0
    end


`TOP <#ts-lua-plugin>`_

Http control channel constants
------------------------------
**context:** do_remap or do_global_* or later

::

    TS_LUA_HTTP_CNTL_GET_LOGGING_MODE
    TS_LUA_HTTP_CNTL_SET_LOGGING_MODE
    TS_LUA_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE
    TS_LUA_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE

`TOP <#ts-lua-plugin>`_

ts.mgmt.get_counter
-------------------
**syntax:** *val = ts.mgmt.get_counter(RECORD_NAME)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to retrieve the record value which has a counter type.

::

    n = ts.mgmt.get_counter('proxy.process.http.incoming_requests')

`TOP <#ts-lua-plugin>`_

ts.mgmt.get_int
---------------
**syntax:** *val = ts.mgmt.get_int(RECORD_NAME)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to retrieve the record value which has a int type.

`TOP <#ts-lua-plugin>`_

ts.mgmt.get_float
-----------------
**syntax:** *val = ts.mgmt.get_float(RECORD_NAME)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to retrieve the record value which has a float type.

`TOP <#ts-lua-plugin>`_

ts.mgmt.get_string
------------------
**syntax:** *val = ts.mgmt.get_string(RECORD_NAME)*

**context:** do_remap or do_global_* or later.

**description:** This function can be used to retrieve the record value which has a string type.

::

    name = ts.mgmt.get_string('proxy.config.product_name')

`TOP <#ts-lua-plugin>`_

Todo
====
* ts.fetch
* ts.cache_xxx
* `support lua-5.2 <https://github.com/portl4t/ts-lua/wiki/support-Lua-5.2>`_

Currently when we use ts_lua as a global plugin, each global hook is using a separate lua state for the same
transaction. This can be wasteful. Also the state cannot be reused for the same transaction across the global hooks. The
alternative will be to use a TXN_START hook to create a lua state first and then add each global hook in the lua script
as transaction hook instead. But this will have problem down the road when we need to have multiple plugins to work
together in some proper orderings. In the future, we should consider different approach, such as creating and
maintaining the lua state in the ATS core. 

`TOP <#ts-lua-plugin>`_

More docs
=========

* https://github.com/portl4t/ts-lua

`TOP <#ts-lua-plugin>`_

