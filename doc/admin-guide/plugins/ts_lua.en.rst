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

.. include:: ../../common.defs

.. _admin-plugins-ts-lua:

TS Lua Plugin
*************

This module embeds Lua, via the standard Lua 5.1 interpreter, into |ATS|. With
this module, we can implement ATS plugin by writing Lua script instead of C
code. Lua code executed using this module can be 100% non-blocking because the
powerful Lua coroutines have been integrated into the ATS event model.

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
            ts.debug(argtb[0] .. ' hostname parameter required!!')
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

This module acts as remap plugin of Traffic Server, so we should realize 'do_remap' or 'do_os_response' function in each
lua script. We can write this in remap.config:

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

We can write this in plugin.config:

::

    tslua.so /etc/trafficserver/script/test_global_hdr.lua

We can also define the number of Lua states to be used for the plugin. If it is used as global plugin, we can write the
following in plugin.config

::

    tslua.so --states=64 /etc/trafficserver/script/test_global_hdr.lua

If it is used as remap plugin, we can write the following in remap.config to define the number of Lua states

::

    map http://a.tbcdn.cn/ http://inner.tbcdn.cn/ @plugin=/XXX/tslua.so @pparam=--states=64 @pparam=/XXX/test_hdr.lua

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

ts.process.uuid
---------------
**syntax:** *val = ts.process.uuid()*

**context:** global

**description:** This function returns the global process id.

Here is an example:

::

    local pid = ts.process.uuid()  -- a436bae6-082c-4805-86af-78a5916c4a91

`TOP <#ts-lua-plugin>`_

ts.now
------
**syntax:** *val = ts.now()*

**context:** global

**description:** This function returns the time since the Epoch (00:00:00 UTC, January 1, 1970), measured in seconds. It
includes milliseconds as the decimal part.

Here is an example:

::

    local nt = ts.now()  -- 1395221053.123

`TOP <#ts-lua-plugin>`_

ts.debug
--------
**syntax:** *ts.debug(TAG?, MESSAGE)*

**context:** global

**description**: Log the MESSAGE to traffic.out if debug TAG is enabled(the default TAG is **ts_lua**).

Here is an example:

::

       ts.debug('I am in do_remap now.')
       ts.debug("scw", "hello world")

We should write this TAG in records.config(If TAG is missing, default TAG will be set):

``CONFIG proxy.config.diags.debug.tags STRING TAG``

`TOP <#ts-lua-plugin>`_

ts.error
--------
**syntax:** *ts.error(MESSAGE)*

**context:** global

**description**: Log the MESSAGE to error.log

Here is an example:

::

       ts.error('This is an error message')

`TOP <#ts-lua-plugin>`_

TS Basic Internal Information
-----------------------------
**syntax:** *ts.get_install_dir()*
**syntax:** *ts.get_runtime_dir()*
**syntax:** *ts.get_config_dir()*
**syntax:** *ts.get_plugin_dir()*
**syntax:** *ts.get_traffic_server_version()*

**context:** global 

**description**: get basic internal information for the TS instance, such as install directory, runtime directory,
config directory, plugin directory and version of ATS

Here is an example:

::

       local config_dir = ts.get_config_dir()

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

ts.remap.get_to_url_host
------------------------
**syntax:** *ts.remap.get_to_url_host()*

**context:** do_remap

**description**: retrieve the "to" host of the remap rule

Here is an example:

::

    function do_remap()
        local to_host = ts.remap.get_to_url_host()
        ts.debug(to_host)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.remap.get_to_url_port
------------------------
**syntax:** *ts.remap.get_to_url_port()*

**context:** do_remap

**description**: retrieve the "to" port of the remap rule

`TOP <#ts-lua-plugin>`_

ts.remap.get_to_url_scheme
--------------------------
**syntax:** *ts.remap.get_to_url_scheme()*

**context:** do_remap

**description**: retrieve the "to" scheme of the remap rule

`TOP <#ts-lua-plugin>`_

ts.remap.get_to_uri
-------------------
**syntax:** *ts.remap.get_to_uri()*

**context:** do_remap

**description**: retrieve the "to" path of the remap rule

`TOP <#ts-lua-plugin>`_

ts.remap.get_to_url
-------------------
**syntax:** *ts.remap.get_to_url()*

**context:** do_remap

**description**: retrieve the "to" url of the remap rule

`TOP <#ts-lua-plugin>`_

ts.remap.get_from_url_host
--------------------------
**syntax:** *ts.remap.get_from_url_host()*

**context:** do_remap

**description**: retrieve the "from" host of the remap rule

`TOP <#ts-lua-plugin>`_

ts.remap.get_from_url_port
--------------------------
**syntax:** *ts.remap.get_from_url_port()*

**context:** do_remap

**description**: retrieve the "from" port of the remap rule

`TOP <#ts-lua-plugin>`_

ts.remap.get_from_url_scheme
----------------------------
**syntax:** *ts.remap.get_from_url_scheme()*

**context:** do_remap

**description**: retrieve the "from" scheme of the remap rule

`TOP <#ts-lua-plugin>`_

ts.remap.get_from_uri
---------------------
**syntax:** *ts.remap.get_from_uri()*

**context:** do_remap

**description**: retrieve the "from" path of the remap rule

`TOP <#ts-lua-plugin>`_

ts.remap.get_from_url
---------------------
**syntax:** *ts.remap.get_from_url()*

**context:** do_remap

**description**: retrieve the "from" url of the remap rule

`TOP <#ts-lua-plugin>`_

ts.hook
-------
**syntax:** *ts.hook(HOOK_POINT, FUNCTION)*

**context:** global or do_remap/do_os_response or do_global_* or later

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

::

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

::

    TS_LUA_HOOK_OS_DNS
    TS_LUA_HOOK_PRE_REMAP
    TS_LUA_HOOK_READ_CACHE_HDR
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

Additional Information:

+-----------------------+---------------------------+----------------------+--------------------+----------------------+
|   Hook Point          | Lua Hook Point constant   |   Hook function be   | Hook function be   |   Hook function be   |
|                       |                           |   registered  within | registered within  |   registered within  |
|                       |                           |   do_remap() via     | do_os_response()   |   global context via |
|                       |                           |   ts.hook()?         | via ts.hook()?     |   ts.hook()?         |
+=======================+===========================+======================+====================+======================+
| TS_HTTP_TXN           | TS_LUA_HOOK               |     NO               |    NO              |    YES               |
| _START_HOOK           | _TXN_START                |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_READ          | TS_LUA_HOOK               |     NO               |    NO              |    YES               |
| _REQUEST_HDR_HOOK     | _READ_REQUEST_HDR         |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_PRE           | TS_LUA_HOOK               |     NO               |    NO              |    YES               |
| _REMAP_HOOK           | _PRE_REMAP                |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_POST          | TS_LUA_HOOK               |     YES              |    NO              |    YES               |
| _REMAP_HOOK           | _POST_REMAP               |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_READ          | TS_LUA_HOOK               |     YES              |    NO              |    YES               |
| _CACHE_HDR_HOOK       | _READ_CACHE_HDR           |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_OS            | TS_LUA_HOOK               |     YES              |    NO              |    YES               |
| _DNS_HOOK             | _OS_DNS                   |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_CACHE         | TS_LUA_HOOK               |     YES              |    NO              |    YES               |
| _LOOKUP_COMPLETE_HOOK | _CACHE_LOOKUP_COMPLETE    |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_SEND          | TS_LUA_HOOK               |     YES              |    NO              |    YES               |
| _REQUEST_HDR_HOOK     | _SEND_REQUEST_HDR         |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_READ          | TS_LUA_HOOK               |     YES              |    YES             |    YES               |
| _RESPONSE_HDR_HOOK    | _READ_RESPONSE_HDR        |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_SEND          | TS_LUA_HOOK               |     YES              |    YES             |    YES               |
| _RESPONSE_HDR_HOOK    | _SEND_RESPONSE_HDR        |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_REQUEST       | TS_LUA_REQUEST_TRANSFORM  |     YES              |    NO              |    YES               |
| _TRANSFORM_HOOK       |                           |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_RESPONSE      | TS_LUA_RESPONSE_TRANSFORM |     YES              |    YES             |    YES               |
| _TRANSFORM_HOOK       |                           |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+
| TS_HTTP_TXN           | TS_LUA_HOOK_TXN_CLOSE     |     YES              |    YES             |    YES               |
| _CLOSE_HOOK           |                           |                      |                    |                      |
+-----------------------+---------------------------+----------------------+--------------------+----------------------+


`TOP <#ts-lua-plugin>`_

ts.ctx
------
**syntax:** *ts.ctx[KEY] = VALUE*

**syntax:** *VALUE = ts.ctx[KEY]*

**context:** do_remap/do_os_response or do_global_* or later

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

**context:** do_remap/do_os_response or do_global_* or later

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

**context:** do_remap/do_os_response or do_global_* or later

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

**context:** do_remap/do_os_response or later

**description:** This function can be used to retrieve the client request's path.

Here is an example:

::

    function do_remap()
        local uri = ts.client_request.get_uri()
        ts.debug(uri)
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

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to retrieve the client request's query string.

Here is an example:

::

    function do_remap()
        local query = ts.client_request.get_uri_args()
        ts.debug(query)
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

ts.client_request.get_uri_params
--------------------------------
**syntax:** *ts.client_request.get_uri_params()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to retrieve the client request's parameter string.

Here is an example:

::

    function do_remap()
        local query = ts.client_request.get_uri_params()
        ts.debug(query)
    end

Then ``GET /st;a=1`` will yield the output:

``a=1``


`TOP <#ts-lua-plugin>`_

ts.client_request.set_uri_params
--------------------------------
**syntax:** *ts.client_request.set_uri_params(PARAMETER_STRING)*

**context:** do_remap or do_global_*

**description:** This function can be used to override the client request's parameter string.

::

    ts.client_request.set_uri_params('n=6')


`TOP <#ts-lua-plugin>`_

ts.client_request.get_url
-------------------------
**syntax:** *ts.client_request.get_url()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to retrieve the whole client request's url.

Here is an example:

::

    function do_remap()
        local url = ts.client_request.get_url()
        ts.debug(url)
    end

Then ``GET /st?a=1&b=2 HTTP/1.1\r\nHost: a.tbcdn.cn\r\n...`` will yield the output:

``http://a.tbcdn.cn/st?a=1&b=2``

`TOP <#ts-lua-plugin>`_

ts.client_request.header.HEADER
-------------------------------
**syntax:** *ts.client_request.header.HEADER = VALUE*

**syntax:** *ts.client_request.header[HEADER] = VALUE*

**syntax:** *VALUE = ts.client_request.header.HEADER*

**context:** do_remap/do_os_response or do_global_* or later

**description:** Set, add to, clear or get the current client request's HEADER.

Here is an example:

::

    function do_remap()
        local ua = ts.client_request.header['User-Agent']
        ts.debug(ua)
        ts.client_request.header['Host'] = 'a.tbcdn.cn'
    end

Then ``GET /st HTTP/1.1\r\nHost: b.tb.cn\r\nUser-Agent: Mozilla/5.0\r\n...`` will yield the output:

``Mozilla/5.0``


`TOP <#ts-lua-plugin>`_

ts.client_request.get_headers
-----------------------------
**syntax:** *ts.client_request.get_headers()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** Returns a Lua table holding all the headers for the current client request.

Here is an example:

::

    function do_remap()
        hdrs = ts.client_request.get_headers()
        for k, v in pairs(hdrs) do
            ts.debug(k..': '..v)
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

**context:** do_remap/do_os_response or do_global_* or later

**description**: This function can be used to get socket address of the client.

The ts.client_request.client_addr.get_addr function returns three values, ip is a string, port and family is number.

Here is an example:

::

    function do_remap()
        ip, port, family = ts.client_request.client_addr.get_addr()
        ts.debug(ip)               -- 192.168.231.17
        ts.debug(port)             -- 17786
        ts.debug(family)           -- 2(AF_INET)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.client_request.client_addr.get_incoming_port
-----------------------------------------------
**syntax:** *ts.client_request.client_addr.get_incoming_port()*

**context:** do_remap/do_os_response or do_global_* or later

**description**: This function can be used to get incoming port of the request.

The ts.client_request.client_addr.get_incoming_port function returns incoming port as number.

Here is an example:

::

    function do_global_read_request()
        port = ts.client_request.client_addr.get_incoming_port()
        ts.debug(port)             -- 80
    end

`TOP <#ts-lua-plugin>`_

ts.client_request.get_url_host
------------------------------
**syntax:** *host = ts.client_request.get_url_host()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** Return the ``host`` field of the request url.

Here is an example:

::

    function do_remap()
        local url_host = ts.client_request.get_url_host()
        ts.debug(url_host)
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

**context:** do_remap/do_os_response or do_global_* or later

**description:** Returns the ``port`` field of the request url as a Lua number.

Here is an example:

::

    function do_remap()
        local url_port = ts.client_request.get_url_port()
        ts.debug(url_port)
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

**context:** do_remap/do_os_response or do_global_* or later

**description:** Return the ``scheme`` field of the request url.

Here is an example:

::

    function do_remap()
        local url_scheme = ts.client_request.get_url_scheme()
        ts.debug(url_scheme)
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

ts.http.get_cache_lookup_url
----------------------------
**syntax:** *ts.http.get_cache_lookup_url()*

**context:** do_global_cache_lookup_complete

**description:** This function can be used to get the cache lookup url for the client request.

Here is an example

::

    function cache_lookup()
        ts.http.set_cache_lookup_url('http://bad.com/bad.html')
        local cache = ts.http.get_cache_lookup_url()
        ts.debug(cache)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.set_cache_lookup_url
----------------------------
**syntax:** *ts.http.set_cache_lookup_url()*

**context:** do_global_cache_lookup_complete

**description:** This function can be used to set the cache lookup url for the client request.

`TOP <#ts-lua-plugin>`_

ts.http.get_parent_proxy
------------------------
**syntax:** *ts.http.get_parent_proxy()*

**context:** do_global_cache_lookup_complete

**description:** This function can be used to get the parent proxy host and port.

Here is an example

::

    function cache_lookup()
        ts.http.set_parent_proxy('test1.test.com', 1111)
        host, port = ts.http.get_parent_proxy()
        ts.debug(host)
        ts.debug(port)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.set_parent_proxy
------------------------
**syntax:** *ts.http.set_parent_proxy()*

**context:** do_global_cache_lookup_complete

**description:** This function can be used to set the parent proxy host and name.

`TOP <#ts-lua-plugin>`_

ts.http.get_parent_selection_url
--------------------------------
**syntax:** *ts.http.get_parent_selection_url()*

**context:** do_global_cache_lookup_complete

**description:** This function can be used to get the parent selection url for the client request.

Here is an example

::

    function cache_lookup()
        ts.http.set_parent_selection_url('http://bad.com/bad.html')
        local cache = ts.http.get_parent_selection_url()
        ts.debug(cache)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.set_parent_selection_url
--------------------------------
**syntax:** *ts.http.set_parent_selection_url()*

**context:** do_global_cache_lookup_complete

**description:** This function can be used to set the parent selection url for the client request.

`TOP <#ts-lua-plugin>`_

ts.http.set_server_resp_no_store
--------------------------------
**syntax:** *ts.http.set_server_resp_no_store(status)*

**context:** do_global_read_response

**description:** This function can be used to signal ATS to not store the response in cache

Here is an example:

::

    function do_global_read_response()
        ts.http.set_server_resp_no_store(1)
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
            ts.debug('hit')
        else
            ts.debug('not hit')
        end
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
        return 0
    end


`TOP <#ts-lua-plugin>`_

ts.http.set_cache_lookup_status
-------------------------------
**syntax:** *ts.http.set_cache_lookup_status()*

**context:** function after TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE hook point

**description:** This function can be used to set cache lookup status.

Here is an example:

::

    function cache_lookup()
        local cache_status = ts.http.get_cache_lookup_status()
        if cache_status == TS_LUA_CACHE_LOOKUP_HIT_FRESH then
            ts.debug('hit')
        else
            ts.debug('not hit')
        end
        ts.http.set_cache_lookup_status(TS_LUA_CACHE_LOOKUP_MISS)
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
            ts.debug(code)         -- 200
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
            ts.debug(ct)         -- text/plain
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
                ts.debug(k..': '..v)
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
        ts.debug(uri)
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
        ts.debug(query)
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

ts.server_request.get_uri_params
--------------------------------
**syntax:** *ts.server_request.get_uri_params()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description:** This function can be used to retrieve the server request's parameter string.

Here is an example:

::

    function send_request()
        local query = ts.server_request.get_uri_params()
        ts.debug(query)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
        return 0
    end

Then ``GET /st;a=1`` will yield the output:

``a=1``


`TOP <#ts-lua-plugin>`_

ts.server_request.set_uri_params
--------------------------------
**syntax:** *ts.server_request.set_uri_params(PARAMETER_STRING)*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point

**description:** This function can be used to override the server request's parameter string.

::

    ts.server_request.set_uri_params('n=6')


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
        ts.debug(ua)
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
            ts.debug(k..': '..v)
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

ts.server_request.server_addr.set_addr
--------------------------------------
**syntax:** *ts.server_request.server_addr.set_addr()*

**context:** no later than function @ TS_LUA_HOOK_OS_DNS hook point

**description**: This function can be used to set socket address of the origin server.

The ts.server_request.server_addr.set_addr function requires three inputs, ip is a string, port and family is number.

Here is an example:

::

    function do_global_read_request()
        ts.server_request.server_addr.set_addr("192.168.231.17", 80, TS_LUA_AF_INET)
    end

`TOP <#ts-lua-plugin>`_

Socket address family
---------------------
**context:** global

::

    TS_LUA_AF_INET (2)
    TS_LUA_AF_INET6 (10)


`TOP <#ts-lua-plugin>`_

ts.server_request.server_addr.get_addr
--------------------------------------
**syntax:** *ts.server_request.server_addr.get_addr()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description**: This function can be used to get socket address of the origin server.

The ts.server_request.server_addr.get_addr function returns three values, ip is a string, port and family is number.

Here is an example:

::

    function do_global_send_request()
        ip, port, family = ts.server_request.server_addr.get_addr()
        ts.debug(ip)               -- 192.168.231.17
        ts.debug(port)             -- 80
        ts.debug(family)           -- 2(AF_INET)
    end

`TOP <#ts-lua-plugin>`_

ts.server_request.server_addr.get_nexthop_addr
----------------------------------------------
**syntax:** *ts.server_request.server_addr.get_nexthop_addr()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description**: This function can be used to get socket address of the next hop to the origin server.

The ts.server_request.server_addr.get_nexthop_addr function returns three values, ip is a string, port and family is number.

Here is an example:

::

    function do_global_send_request()
        ip, port, family = ts.server_request.server_addr.get_nexthop_addr()
        ts.debug(ip)               -- 192.168.231.17
        ts.debug(port)             -- 80
        ts.debug(family)           -- 2(AF_INET)
    end

`TOP <#ts-lua-plugin>`_

ts.server_request.server_addr.get_ip
------------------------------------
**syntax:** *ts.server_request.server_addr.get_ip()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description**: This function can be used to get ip address of the origin server.

The ts.server_request.server_addr.get_ip function returns ip as a string.

Here is an example:

::

    function do_global_send_request()
        ip = ts.server_request.server_addr.get_ip()
        ts.debug(ip)               -- 192.168.231.17
    end

`TOP <#ts-lua-plugin>`_

ts.server_request.server_addr.get_port
--------------------------------------
**syntax:** *ts.server_request.server_addr.get_port()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description**: This function can be used to get port of the origin server.

The ts.server_request.server_addr.get_port function returns port as number.

Here is an example:

::

    function do_global_send_request()
        port = ts.server_request.server_addr.get_port()
        ts.debug(port)             -- 80
    end

`TOP <#ts-lua-plugin>`_

ts.server_request.server_addr.get_outgoing_port
-----------------------------------------------
**syntax:** *ts.server_request.server_addr.get_outgoing_port()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point or later

**description**: This function can be used to get outgoing port to the origin server.

The ts.server_request.server_addr.get_outgoing_port function returns outgoing port as number.

Here is an example:

::

    function do_global_send_request()
        port = ts.server_request.server_addr.get_outgoing_port()
        ts.debug(port)             -- 50880
    end

`TOP <#ts-lua-plugin>`_

ts.server_request.server_addr.set_outgoing_addr
-----------------------------------------------
**syntax:** *ts.server_request.server_addr.set_outgoing_addr()*

**context:** earlier than or inside function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point

**description**: This function can be used to set outgoing socket address for the request to origin.

The ts.server_request.server_addr.set_outgoing_addr function requires three inputs, ip is a string, port and family is number.

Here is an example:

::

    function do_global_send_request()
        ts.server_request.server_addr.set_outgoing_addr("192.168.231.17", 80, TS_LUA_AF_INET)
    end

`TOP <#ts-lua-plugin>`_

ts.server_request.get_url_host
------------------------------
**syntax:** *host = ts.server_request.get_url_host()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point

**description:** Return the ``host`` field of the request url.

Here is an example:

::

    function send_request()
        local url_host = ts.server_request.get_url_host()
        ts.debug(url_host)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
        return 0
    end

Then ``GET http://abc.com/p2/a.txt HTTP/1.1`` will yield the output:

``abc.com``

`TOP <#ts-lua-plugin>`_

ts.server_request.set_url_host
------------------------------
**syntax:** *ts.server_request.set_url_host(str)*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point

**description:** Set ``host`` field of the request url with ``str``. This function is used to change the host name in the GET request to next tier

Here is an example:

::

    function send_request()
        ts.server_request.set_url_host("")
        ts.server_request.set_url_scheme("")
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
        return 0
    end

The GET request like this:

::

    +++++++++ Proxy's Request +++++++++
    – State Machine Id: 5593
    GET http://origin.com/dir1/a.txt HTTP/1.1
    User-Agent: curl/7.29.0
    Host: abc.com
    Accept: /
    Client-ip: 135.xx.xx.xx
    X-Forwarded-For: 135.xx.xx.xx

Will be changed to:

::

    +++++++++ Proxy's Request +++++++++
    – State Machine Id: 5593
    GET /dir1/a.txt HTTP/1.1
    User-Agent: curl/7.29.0
    Host: abc.com
    Accept: /
    Client-ip: 135.xx.xx.xx
    X-Forwarded-For: 135.xx.xx.xx

`TOP <#ts-lua-plugin>`_

ts.server_request.get_url_scheme
--------------------------------
**syntax:** *scheme = ts.server_request.get_url_scheme()*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point

**description:** Return the ``scheme`` field of the request url.

Here is an example:

::

    function send_request()
        local url_scheme = ts.server_request.get_url_scheme()
        ts.debug(url_host)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
        return 0
    end

Then ``GET /liuyurou.txt HTTP/1.1\r\nHost: 192.168.231.129:8080\r\n...`` will yield the output:

``http``

`TOP <#ts-lua-plugin>`_

ts.server_request.set_url_scheme
--------------------------------
**syntax:** *ts.server_request.set_url_scheme(str)*

**context:** function @ TS_LUA_HOOK_SEND_REQUEST_HDR hook point

**description:** Set ``scheme`` field of the request url with ``str``. This function is used to change the scheme of the server request.

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
        ts.debug(code)         -- 200
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
        ts.debug(ct)
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
            ts.debug(k..': '..v)
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
        ts.debug(code)         -- 200
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
        ts.debug(ct)
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
            ts.debug(k..': '..v)
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

Number constants
----------------------
**context:** global

::

    TS_LUA_INT64_MAX (9223372036854775808)
    TS_LUA_INT64_MIN (-9223372036854775808L)

These constants are usually used in transform handler.

`TOP <#ts-lua-plugin>`_

ts.http.resp_cache_transformed
------------------------------
**syntax:** *ts.http.resp_cache_transformed(BOOL)*

**context:** do_remap/do_os_response or do_global_* or later

**description**: This function can be used to tell trafficserver whether to cache the transformed data.

Here is an example:

::

    function upper_transform(data, eos)
        return string.upper(data), eos
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

**context:** do_remap/do_os_response or do_global_* or later

**description**: This function can be used to tell trafficserver whether to cache the untransformed data.

Here is an example:

::

    function upper_transform(data, eos)
        return string.upper(data), eos
    end

    function do_remap()
        ts.hook(TS_LUA_RESPONSE_TRANSFORM, upper_transform)
        ts.http.resp_cache_untransformed(1)
        return 0
    end

This function is usually called after we hook TS_LUA_RESPONSE_TRANSFORM.


`TOP <#ts-lua-plugin>`_

ts.http.resp_transform.get_upstream_bytes
-----------------------------------------
**syntax:** *ts.http.resp_transform.get_upstream_bytes()*

**context:** transform handler

**description**: This function can be used to retrive the total bytes to be received from the upstream. If we got
chunked response body from origin server, TS_LUA_INT64_MAX will be returned.

Here is an example:

::

    local APPEND_DATA = 'TAIL\n'

    function append_transform(data, eos)
        if ts.ctx['len_set'] == nil then
            local sz = ts.http.resp_transform.get_upstream_bytes()
            if sz ~= TS_LUA_INT64_MAX then
                ts.http.resp_transform.set_downstream_bytes(sz + string.len(APPEND_DATA))
            end

            ts.ctx['len_set'] = true
        end

        if eos == 1 then
            return data .. APPEND_DATA, eos
        else
            return data, eos
        end
    end

    function do_remap()
        ts.hook(TS_LUA_RESPONSE_TRANSFORM, append_transform)
        ts.http.resp_cache_transformed(0)
        ts.http.resp_cache_untransformed(1)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.resp_transform.set_downstream_bytes
-------------------------------------------
**syntax:** *ts.http.resp_transform.set_downstream_bytes(NUMBER)*

**context:** transform handler

**description**: This function can be used to set the total bytes to be sent to the downstream.

Sometimes we want to set Content-Length header in client_response, and this function should be called before any real
data is returned from the transform handler.


`TOP <#ts-lua-plugin>`_

ts.http.skip_remapping_set
--------------------------
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

`TOP <#ts-lua-plugin>`_

ts.http.get_client_protocol_stack
---------------------------------
**syntax:** *ts.http.get_client_protocol_stack()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to get client protocol stack information

Here is an example:

::

    function do_global_read_request()
        local stack = {ts.http.get_client_protocol_stack()}
        for k,v in pairs(stack) do
          ts.debug(v)
        end
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.server_push
-------------------
**syntax:** *ts.http.server_push()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can do http/2 server push for the input url

Here is an example:

::

    function do_global_read_request()
        ts.http.server_push("https://test.com/test.js")
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.is_websocket
--------------------
**syntax:** *ts.http.is_websocket()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to tell if the transacton is websocket

Here is an example:

::

    function do_global_read_request()
        local flag = ts.http.is_websocket()
        ts.debug(flag)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.get_plugin_tag
----------------------
**syntax:** *ts.http.get_plugin_tag()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to get plugin tag of a transaction

Here is an example:

::

    function do_global_read_request()
        local tag = ts.http.get_plugin_tag() or ''
        ts.debug(tag)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.id
----------
**syntax:** *ts.http.id()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to tell id of a transaction

Here is an example:

::

    function do_global_read_request()
        local id = ts.http.id()
        ts.debug(id)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.is_internal_request
---------------------------
**syntax:** *ts.http.is_internal_request()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to tell is a request is internal or not

Here is an example:

::

    function do_global_read_request()
        local internal = ts.http.is_internal_request()
        ts.debug(internal)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.transaction_count
-------------------------
**syntax:** *ts.http.transaction_count()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function returns the number of transaction in this connection

Here is an example

::

    function do_remap()
        local count = ts.http.transaction_count()
        ts.debug(tostring(count))
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.redirect_url_set
------------------------
**syntax:** *ts.http.redirect_url_set()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function sets the redirect url and instructs the transaction to follow the redirection as response

Here is an example

::

    function do_global_read_response()
        ts.http.redirect_url_set('http://foo.com')
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.get_server_state
------------------------
**syntax:** *ts.http.get_server_state()*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function returns the current server state

Here is an example

::

    function do_os_response()
        local result = ts.http.get_server_state()
        if result == TS_LUA_SRVSTATE_CONNECTION_ALIVE then
          ts.debug('Alive')
        end
    end

`TOP <#ts-lua-plugin>`_

Server state constants
----------------------
**context:** global

::

    TS_LUA_SRVSTATE_STATE_UNDEFINED (0)
    TS_LUA_SRVSTATE_ACTIVE_TIMEOUT (1)
    TS_LUA_SRVSTATE_BAD_INCOMING_RESPONSE (2)
    TS_LUA_SRVSTATE_CONNECTION_ALIVE (3)
    TS_LUA_SRVSTATE_CONNECTION_CLOSED (4)
    TS_LUA_SRVSTATE_CONNECTION_ERROR (5)
    TS_LUA_SRVSTATE_INACTIVE_TIMEOUT(6)
    TS_LUA_SRVSTATE_OPEN_RAW_ERROR (7)
    TS_LUA_SRVSTATE_PARSE_ERROR (8)
    TS_LUA_SRVSTATE_TRANSACTION_COMPLETE (9)
    TS_LUA_SRVSTATE_CONGEST_CONTROL_CONGESTED_ON_F (10)
    TS_LUA_SRVSTATE_CONGEST_CONTROL_CONGESTED_ON_M (11)

`TOP <#ts-lua-plugin>`_

ts.http.get_remap_from_url
--------------------------
**syntax:** *ts.http.get_remap_from_url()*

**context:** do_global_post_remap

**description:** This function can be used to get the *from* URL in the matching line in :file:`remap.config`.

Here is an example

::

    function do_global_post_remap()
        local from_url = ts.http.get_remap_from_url()
        ts.debug(from_url)
    end

`TOP <#ts-lua-plugin>`_

ts.http.get_remap_to_url
------------------------
**syntax:** *ts.http.get_remap_to_url()*

**context:** do_global_post_remap

**description:** This function can be used to get the *to* URL in the matching line in :file:`remap.config`.

Here is an example

::

    function do_global_post_remap()
        local to_url = ts.http.get_remap_to_url()
        ts.debug(to_url)
    end

`TOP <#ts-lua-plugin>`_

ts.http.get_client_fd
---------------------
**syntax:** *ts.http.get_client_fd()*

**context:** after do_global_read_request

**description:** This function can be used to get the client FD for the transaction.

Here is an example

::

    function do_global_read_request()
        local fd = ts.http.get_client_fd()
        ts.debug(fd)
    end

`TOP <#ts-lua-plugin>`_

ts.http.get_server_fd
---------------------
**syntax:** *ts.http.get_server_fd()*

**context:** after do_global_send_request

**description:** This function can be used to get the server (origin) FD for the transaction.

Here is an example

::

    function do_global_send_request()
        local fd = ts.http.get_server_fd()
        ts.debug(fd)
    end

`TOP <#ts-lua-plugin>`_

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
        ts.debug(nt.t9(7979))
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
        ts.debug(ma.ft())
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
        ts.debug(uri)              -- /foo
        ts.debug(ts.md5(uri))      -- 1effb2475fcfba4f9e8b8a1dbc8f3caf
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
        ts.debug(uri)              -- /foo
        ts.debug(ts.sha1(uri))     -- 6dbd548cc03e44b8b44b6e68e56255ce4273ae49
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

ts.base64_encode
----------------
**syntax:** *value = ts.base64_encode(str)*

**context:** global

**description:** Returns the base64 encoding of the ``str`` argument.

Here is an example:

::

    function do_remap()
        uri = ts.client_request.get_uri()
        value = ts.base64_encode(uri)
    end


`TOP <#ts-lua-plugin>`_

ts.base64_decode
----------------
**syntax:** *value = ts.base64_decode(str)*

**context:** global

**description:** Returns the base64 decoding of the ``str`` argument.

Here is an example:

::

    function do_remap()
        uri = ts.client_request.get_uri()
        encoded_value = ts.base64_encode(uri)
        decoded_value = ts.base64_decode(encoded_value)
    end


`TOP <#ts-lua-plugin>`_

ts.escape_uri
-------------
**syntax:** *value = ts.escape_uri(str)*

**context:** global

**description:** Returns the uri-escaped value of the ``str`` argument.

Here is an example:

::

    function do_remap()
        test = '/some value/'
        value = ts.escape_uri(test)
    end

`TOP <#ts-lua-plugin>`_

ts.unescape_uri
---------------
**syntax:** *value = ts.unescape_uri(str)*

**context:** global

**description:** Returns the uri-unescaped value of the ``str`` argument.

Here is an example:

::

    function do_remap()
        test = '/some value/'
        escaped_value = ts.escape_uri(test)
        unescaped_value = ts.unescape_uri(escaped_value)
    end


`TOP <#ts-lua-plugin>`_

ts.fetch
-----------
**syntax:** *res = ts.fetch(url, table?)*

**context:** hook point functions added after do_remap

**description:** Issues a synchronous but still non-block http request with the ``url`` and the optional ``table``.

Returns a Lua table with serveral slots (res.status, res.header, res.body, and res.truncated).

``res.status`` holds the response status code.

``res.header`` holds the response header table.

``res.body`` holds the response body which may be truncated, you need to check res.truncated to see if the data is
truncated.

Here is a basic example:

::

    function post_remap()
        local url = string.format('http://%s/foo.txt', ts.ctx['host'])
        local res = ts.fetch(url)
        if res.status == 200 then
            ts.debug(res.body)
        end
    end

    function do_remap()
        local inner = ts.http.is_internal_request()
        if inner ~= 0 then
            return 0
        end
        local host = ts.client_request.header['Host']
        ts.ctx['host'] = host
        ts.hook(TS_LUA_HOOK_POST_REMAP, post_remap)
    end

We can set the optional table with serveral members:

``header`` holds the request header table.

``method`` holds the request method. The default method is 'GET'.

``cliaddr`` holds the request client address in ip:port form. The default cliaddr is '127.0.0.1:33333'

Issuing a post request:

::

    res = ts.fetch('http://xx.com/foo', {method = 'POST', body = 'hello world'})

`TOP <#ts-lua-plugin>`_

ts.fetch_multi
--------------
**syntax:** *vec = ts.fetch_multi({{url, table?}, {url, table?}, ...})*

**context:** hook point functions added after do_remap

Just like `ts.fetch`, but supports multiple http requests running in parallel.

This function will fetch all the urls specified by the input table and return a table which contain all the results in
the same order.

Here is an example:

::

    local vec = ts.fetch_multi({
                    {'http://xx.com/slayer'},
                    {'http://xx.com/am', {cliaddr = '192.168.1.19:35423'}},
                    {'http://xx.com/naga', {method = 'POST', body = 'hello world'}},
                })

    for i = 1, #(vec) do
        ts.debug(vec[i].status)
    end


`TOP <#ts-lua-plugin>`_


ts.http.intercept
-----------------
**syntax:** *ts.http.intercept(FUNCTION, param1?, param2?, ...)*

**context:** do_remap or do_global_*

**description:** Intercepts the client request and processes it in FUNCTION with optional params.

We should construct the response for the client request, and the request will not be processed by other modules, like
hostdb, cache, origin server...

Intercept FUNCTION will be executed in a new lua_thread, so we can delivery optional params from old lua_thread to new
lua_thread if needed.

Here is an example:

::

    require 'os'

    function send_data(dstr)
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
        ts.debug(dstr)
        ts.say(resp)
    end

    function do_remap()
        ts.http.intercept(send_data, 'hello world')
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

ts.http.server_intercept
------------------------
**syntax:** *ts.http.server_intercept(FUNCTION, param1?, param2?, ...)*

**context:** do_remap or do_global_*

**description:** Intercepts the server request and acts as the origin server.

Just like ts.http.intercept, but this function will intercept the server request, and we can acts as the origin server
in `FUNCTION`.

Here is an example:

::

    require 'os'

    function process_combo(host)
        local url1 = string.format('http://%s/css/1.css', host)
        local url2 = string.format('http://%s/css/2.css', host)
        local url3 = string.format('http://%s/css/3.css', host)

        local hdr = {
            ['Host'] = host,
            ['User-Agent'] = 'blur blur',
        }

        local ct = {
            header = hdr,
            method = 'GET'
        }

        local arr = ts.fetch_multi(
                {
                    {url1, ct},
                    {url2, ct},
                    {url3, ct},
                })

        local ctype = arr[1].header['Content-Type']
        local body = arr[1].body .. arr[2].body .. arr[3].body

        local resp =  'HTTP/1.1 200 OK\r\n' ..
                      'Server: ATS/5.2.0\r\n' ..
                      'Last-Modified: ' .. os.date("%a, %d %b %Y %H:%M:%S GMT", os.time()) .. '\r\n' ..
                      'Cache-Control: max-age=7200\r\n' ..
                      'Accept-Ranges: bytes\r\n' ..
                      'Content-Type: ' .. ctype .. '\r\n' ..
                      'Content-Length: ' .. string.format('%d', string.len(body)) .. '\r\n\r\n' ..
                      body

        ts.say(resp)
    end

    function do_remap()
        local inner =  ts.http.is_internal_request()
        if inner ~= 0 then
            return 0
        end

        local h = ts.client_request.header['Host']
        ts.http.server_intercept(process_combo, h)
    end

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

**context:** *hook point functions added after do_remap*

**description:** Sleeps for the specified seconds without blocking.

Behind the scene, this method makes use of the ATS event model.

Here is an example:

::

    function send_response()
        ts.sleep(3)
    end

    function read_response()
        ts.sleep(3)
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_READ_RESPONSE_HDR, read_response)
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
    end

`TOP <#ts-lua-plugin>`_

ts.host_lookup
--------------
**syntax:** *ts.host_lookup(hostname)*

**context:** *hook point functions added after do_remap*

**description:** Look for ip address of the host name without blocking. Nil if address cannot be found.

Behind the scene, this method makes use of the ATS event model.

Here is an example:

::

    function send_response()
        local result = ts.host_lookup("www.xyz.com") -- ip address of www.xyz.com
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
    end

`TOP <#ts-lua-plugin>`_

ts.schedule
-----------
**syntax:** *ts.schedule(THREAD_TYPE, sec, FUNCTION, param1?, param2?, ...)*

**context:** *hook point functions added after do_remap*

**description:** Schedule function to be run after specified seconds without blocking.

Behind the scene, this method makes use of the ATS event model.

Here is an example:

::

    function schedule()
        ts.debug('test schedule starts')
    end

    function cache_lookup()
        ts.debug('cache-lookup')
        ts.schedule(TS_LUA_THREAD_POOL_NET, 0, schedule)
        return 0
    end

    function do_remap()
        ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
    end

`TOP <#ts-lua-plugin>`_

ts.http.config_int_get
----------------------
**syntax:** *val = ts.http.config_int_get(CONFIG)*

**context:** do_remap/do_os_response or do_global_* or later

**description:** Configuration option which has a int value can be retrieved with this function.

::

    val = ts.http.config_int_get(TS_LUA_CONFIG_HTTP_CACHE_HTTP)


`TOP <#ts-lua-plugin>`_

ts.http.config_int_set
----------------------
**syntax:** *ts.http.config_int_set(CONFIG, NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later

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

**context:** do_remap/do_os_response or do_global_* or later

**description:** Configuration option which has a float value can be retrieved with this function.


`TOP <#ts-lua-plugin>`_

ts.http.config_float_set
------------------------
**syntax:** *ts.http.config_float_set(CONFIG, NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to overwrite the configuration options.


`TOP <#ts-lua-plugin>`_

ts.http.config_string_get
-------------------------
**syntax:** *val = ts.http.config_string_get(CONFIG)*

**context:** do_remap/do_os_response or do_global_* or later

**description:** Configuration option which has a string value can be retrieved with this function.


`TOP <#ts-lua-plugin>`_

ts.http.config_string_set
-------------------------
**syntax:** *ts.http.config_string_set(CONFIG, NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later

**description:** This function can be used to overwrite the configuration options.


`TOP <#ts-lua-plugin>`_

Http config constants
---------------------
**context:** do_remap/do_os_response or do_global_* or later

::

    TS_LUA_CONFIG_URL_REMAP_PRISTINE_HOST_HDR
    TS_LUA_CONFIG_HTTP_CHUNKING_ENABLED
    TS_LUA_CONFIG_HTTP_NEGATIVE_CACHING_ENABLED
    TS_LUA_CONFIG_HTTP_NEGATIVE_CACHING_LIFETIME
    TS_LUA_CONFIG_HTTP_CACHE_WHEN_TO_REVALIDATE
    TS_LUA_CONFIG_HTTP_KEEP_ALIVE_ENABLED_IN
    TS_LUA_CONFIG_HTTP_KEEP_ALIVE_ENABLED_OUT
    TS_LUA_CONFIG_HTTP_KEEP_ALIVE_POST_OUT
    TS_LUA_CONFIG_HTTP_SERVER_SESSION_SHARING_MATCH
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
    TS_LUA_CONFIG_HTTP_INSERT_FORWARDED
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
    TS_LUA_CONFIG_HTTP_BACKGROUND_FILL_ACTIVE_TIMEOUT
    TS_LUA_CONFIG_HTTP_RESPONSE_SERVER_STR
    TS_LUA_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR
    TS_LUA_CONFIG_HTTP_CACHE_FUZZ_PROBABILITY
    TS_LUA_CONFIG_HTTP_BACKGROUND_FILL_COMPLETED_THRESHOLD
    TS_LUA_CONFIG_NET_SOCK_PACKET_MARK_OUT
    TS_LUA_CONFIG_NET_SOCK_PACKET_TOS_OUT
    TS_LUA_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE
    TS_LUA_CONFIG_HTTP_CHUNKING_SIZE
    TS_LUA_CONFIG_HTTP_FLOW_CONTROL_ENABLED
    TS_LUA_CONFIG_HTTP_FLOW_CONTROL_LOW_WATER_MARK
    TS_LUA_CONFIG_HTTP_FLOW_CONTROL_HIGH_WATER_MARK
    TS_LUA_CONFIG_HTTP_CACHE_RANGE_LOOKUP
    TS_LUA_CONFIG_HTTP_DEFAULT_BUFFER_SIZE
    TS_LUA_CONFIG_HTTP_DEFAULT_BUFFER_WATER_MARK
    TS_LUA_CONFIG_HTTP_REQUEST_HEADER_MAX_SIZE
    TS_LUA_CONFIG_HTTP_RESPONSE_HEADER_MAX_SIZE
    TS_LUA_CONFIG_HTTP_NEGATIVE_REVALIDATING_ENABLED
    TS_LUA_CONFIG_HTTP_NEGATIVE_REVALIDATING_LIFETIME
    TS_LUA_CONFIG_SSL_HSTS_MAX_AGE
    TS_LUA_CONFIG_SSL_HSTS_INCLUDE_SUBDOMAINS
    TS_LUA_CONFIG_HTTP_CACHE_OPEN_READ_RETRY_TIME
    TS_LUA_CONFIG_HTTP_CACHE_MAX_OPEN_READ_RETRIES
    TS_LUA_CONFIG_HTTP_CACHE_RANGE_WRITE
    TS_LUA_CONFIG_HTTP_POST_CHECK_CONTENT_LENGTH_ENABLED
    TS_LUA_CONFIG_HTTP_GLOBAL_USER_AGENT_HEADER
    TS_LUA_CONFIG_HTTP_AUTH_SERVER_SESSION_PRIVATE
    TS_LUA_CONFIG_HTTP_SLOW_LOG_THRESHOLD
    TS_LUA_CONFIG_HTTP_CACHE_GENERATION
    TS_LUA_CONFIG_BODY_FACTORY_TEMPLATE_BASE
    TS_LUA_CONFIG_HTTP_CACHE_OPEN_WRITE_FAIL_ACTION
    TS_LUA_CONFIG_HTTP_NUMBER_OF_REDIRECTIONS
    TS_LUA_CONFIG_HTTP_CACHE_MAX_OPEN_WRITE_RETRIES
    TS_LUA_CONFIG_HTTP_NORMALIZE_AE
    TS_LUA_CONFIG_LAST_ENTRY

`TOP <#ts-lua-plugin>`_

ts.http.timeout_set
-------------------
**syntax:** *ts.http.timeout_set(CONFIG, NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to overwrite the timeout settings.

Here is an example:

::

    function do_remap()
        ts.http.timeout_set(TS_LUA_TIMEOUT_DNS, 30)    -- 30 seconds
        return 0
    end


`TOP <#ts-lua-plugin>`_

Timeout constants
-----------------
**context:** do_remap/do_os_response or do_global_* or later

::

    TS_LUA_TIMEOUT_ACTIVE
    TS_LUA_TIMEOUT_DNS
    TS_LUA_TIMEOUT_CONNECT
    TS_LUA_TIMEOUT_NO_ACTIVITY


`TOP <#ts-lua-plugin>`_

ts.http.client_packet_mark_set
------------------------------
**syntax:** *ts.http.client_packet_mark_set(NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to set packet mark for client connection.

Here is an example:

::

    function do_remap()
        ts.http.client_packet_mark_set(0)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.http.server_packet_mark_set
------------------------------
**syntax:** *ts.http.server_packet_mark_set(NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to set packet mark for server connection.


`TOP <#ts-lua-plugin>`_

ts.http.client_packet_tos_set
-----------------------------
**syntax:** *ts.http.client_packet_tos_set(NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to set packet tos for client connection.


`TOP <#ts-lua-plugin>`_

ts.http.server_packet_tos_set
-----------------------------
**syntax:** *ts.http.server_packet_tos_set(NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to set packet tos for server connection.


`TOP <#ts-lua-plugin>`_

ts.http.client_packet_dscp_set
------------------------------
**syntax:** *ts.http.client_packet_dscp_set(NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to set packet dscp for client connection.


`TOP <#ts-lua-plugin>`_

ts.http.server_packet_dscp_set
------------------------------
**syntax:** *ts.http.server_packet_dscp_set(NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to set packet dscp for server connection.


`TOP <#ts-lua-plugin>`_

ts.http.enable_redirect
-----------------------
**syntax:** *ts.http.enable_redirect(NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**decription:** This function can be used to make transaction follow redirect

Here is an example:

::

    function do_remap()
        ts.http.enable_redirect(1)
        return 0
    end


`TOP <#ts-lua-plugin>`_

ts.http.set_debug
-----------------
**syntax:** *ts.http.set_debug(NUMBER)*

**context:** do_remap/do_os_response or do_global_* or later.

**decription:** This function can be used to enable debug log for the transaction

Here is an example:

::

    function do_remap()
        ts.http.set_debug(1)
        return 0
    end


`TOP <#ts-lua-plugin>`_

ts.http.cntl_get
----------------
**syntax:** *val = ts.http.cntl_get(CNTL_TYPE)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to retireve the value of control channel.

::

    val = ts.http.cntl_get(TS_LUA_HTTP_CNTL_GET_LOGGING_MODE)


`TOP <#ts-lua-plugin>`_

ts.http.cntl_set
----------------
**syntax:** *ts.http.cntl_set(CNTL_TYPE, BOOL)*

**context:** do_remap/do_os_response or do_global_* or later.

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
**context:** do_remap/do_os_response or do_global_* or later

::

    TS_LUA_HTTP_CNTL_GET_LOGGING_MODE
    TS_LUA_HTTP_CNTL_SET_LOGGING_MODE
    TS_LUA_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE
    TS_LUA_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE


`TOP <#ts-lua-plugin>`_

ts.http.milestone_get
---------------------
**syntax:** *val = ts.http.milestone_get(MILESTONE_TYPE)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to retireve the various milestone times. They are how long the
transaction took to traverse portions of the HTTP state machine. Each milestone value is a fractional number
of seconds since the beginning of the transaction.

::

    val = ts.http.milestone_get(TS_LUA_MILESTONE_SM_START)

`TOP <#ts-lua-plugin>`_

Milestone constants
------------------------------
**context:** do_remap/do_os_response or do_global_* or later

::

    TS_LUA_MILESTONE_UA_BEGIN
    TS_LUA_MILESTONE_UA_FIRST_READ
    TS_LUA_MILESTONE_UA_READ_HEADER_DONE
    TS_LUA_MILESTONE_UA_BEGIN_WRITE
    TS_LUA_MILESTONE_UA_CLOSE
    TS_LUA_MILESTONE_SERVER_FIRST_CONNECT
    TS_LUA_MILESTONE_SERVER_CONNECT
    TS_LUA_MILESTONE_SERVER_CONNECT_END
    TS_LUA_MILESTONE_SERVER_BEGIN_WRITE
    TS_LUA_MILESTONE_SERVER_FIRST_READ
    TS_LUA_MILESTONE_SERVER_READ_HEADER_DONE
    TS_LUA_MILESTONE_SERVER_CLOSE
    TS_LUA_MILESTONE_CACHE_OPEN_READ_BEGIN
    TS_LUA_MILESTONE_CACHE_OPEN_READ_END
    TS_LUA_MILESTONE_CACHE_OPEN_WRITE_BEGIN
    TS_LUA_MILESTONE_CACHE_OPEN_WRITE_END
    TS_LUA_MILESTONE_DNS_LOOKUP_BEGIN
    TS_LUA_MILESTONE_DNS_LOOKUP_END
    TS_LUA_MILESTONE_SM_START
    TS_LUA_MILESTONE_SM_FINISH
    TS_LUA_MILESTONE_PLUGIN_ACTIVE
    TS_LUA_MILESTONE_PLUGIN_TOTAL
    TS_LUA_MILESTONE_TLS_HANDSHAKE_START
    TS_LUA_MILESTONE_TLS_HANDSHAKE_END


`TOP <#ts-lua-plugin>`_

ts.mgmt.get_counter
-------------------
**syntax:** *val = ts.mgmt.get_counter(RECORD_NAME)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to retrieve the record value which has a counter type.

::

    n = ts.mgmt.get_counter('proxy.process.http.incoming_requests')

`TOP <#ts-lua-plugin>`_

ts.mgmt.get_int
---------------
**syntax:** *val = ts.mgmt.get_int(RECORD_NAME)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to retrieve the record value which has a int type.

`TOP <#ts-lua-plugin>`_

ts.mgmt.get_float
-----------------
**syntax:** *val = ts.mgmt.get_float(RECORD_NAME)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to retrieve the record value which has a float type.

`TOP <#ts-lua-plugin>`_

ts.mgmt.get_string
------------------
**syntax:** *val = ts.mgmt.get_string(RECORD_NAME)*

**context:** do_remap/do_os_response or do_global_* or later.

**description:** This function can be used to retrieve the record value which has a string type.

::

    name = ts.mgmt.get_string('proxy.config.product_name')

`TOP <#ts-lua-plugin>`_

ts.stat_create
--------------
**syntax:** *val = ts.stat_create(STAT_NAME, RECORDDATA_TYPE, PERSISTENT, SYNC)*

**context:** global

**description:** This function can be used to create a statistics record given the name, data type, persistent
requirement, and sync requirement. A statistics record table will be created with 4 functions to increment,
decrement, get and set the value.

::

    stat:increment(value)
    stat:decrement(value)
    v = stat:get_value()
    stat:set_value(value)

Here is an example.

::

    local test_stat;

    function __init__(args)
        test_stat = ts.stat_create("test_stat",
          TS_LUA_RECORDDATATYPE_INT,
          TS_LUA_STAT_PERSISTENT,
          TS_LUA_STAT_SYNC_COUNT)
    end

    function do_global_read_request()
        local value = test_stat:get_value()
        ts.debug(value)
        test_stat:increment(1)
        return 0
    end

`TOP <#ts-lua-plugin>`_

ts.stat_find
------------
**syntax:** *val = ts.stat_create(STAT_NAME)*

**context:** global

**description:** This function can be used to find a statistics record given the name. A statistics record table will
be returned with 4 functions to increment, decrement, get and set the value. That is similar to ts.stat_create()

`TOP <#ts-lua-plugin>`_

Todo
====
* ts.cache_xxx
* protocol

Currently when we use ts_lua as a global plugin, each global hook is using a separate lua state for the same
transaction. This can be wasteful. Also the state cannot be reused for the same transaction across the global hooks. The
alternative will be to use a TXN_START hook to create a lua state first and then add each global hook in the lua script
as transaction hook instead. But this will have problem down the road when we need to have multiple plugins to work
together in some proper orderings. In the future, we should consider different approach, such as creating and
maintaining the lua state in the ATS core.

`TOP <#ts-lua-plugin>`_

Notes on Unit Testing Lua scripts for ATS Lua Plugin
====================================================

Follow the steps below to use busted framework to run some unit tests on sample scripts and modules

* Build and install lua 5.1.5 using the source code from here - http://www.lua.org/ftp/lua-5.1.tar.gz

* Build and install luarocks 2.2.2 from here - https://github.com/keplerproject/luarocks/wiki/Download

* Run "sudo luarocks install busted"

* Run "sudo luarocks install luacov"

* "cd trafficserver/plugins/experimental/ts_lua/ci"

* Run "busted -c module_test.lua; luacov". It will produce "luacov.report.out" containing the code coverage for "module.lua"

* Run "busted -c script_test.lua; luacov". It will produce "luacov.report.out" containing the code coverage for "script.lua"

Reference for further information

* Busted - http://olivinelabs.com/busted/

* Specifications for asserts/mocks/stubs/etc inside busted framework:
  https://github.com/Olivine-Labs/luassert/tree/master/spec

* luacov - https://luarocks.org/modules/hisham/luacov

`TOP <#ts-lua-plugin>`_

More docs
=========

* https://github.com/portl4t/ts-lua

`TOP <#ts-lua-plugin>`_

