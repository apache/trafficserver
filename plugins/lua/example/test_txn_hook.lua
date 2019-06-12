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

function do_global_txn_start()
    ts.debug('global_txn_start')

    ts.hook(TS_LUA_HOOK_READ_REQUEST_HDR, read_request)
    ts.hook(TS_LUA_HOOK_SEND_REQUEST_HDR, send_request)
    ts.hook(TS_LUA_HOOK_SEND_RESPONSE_HDR, send_response)
    ts.hook(TS_LUA_HOOK_READ_RESPONSE_HDR, read_response)
    ts.hook(TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE, cache_lookup)
    ts.hook(TS_LUA_HOOK_PRE_REMAP, pre_remap)
    ts.hook(TS_LUA_HOOK_POST_REMAP, post_remap)
    ts.hook(TS_LUA_HOOK_SELECT_ALT, select_alt)
    ts.hook(TS_LUA_HOOK_OS_DNS, os_dns)
    ts.hook(TS_LUA_HOOK_READ_CACHE_HDR, read_cache)
    ts.hook(TS_LUA_HOOK_TXN_CLOSE, txn_close)

    return 0
end

function read_request()
    ts.debug('read_request')

    return 0
end

function send_request()
    ts.debug('send_request')

    return 0
end

function read_response()
    ts.debug('read_response')

    return 0
end

function send_response()
    ts.debug('send_response')

    return 0
end

function post_remap()
    ts.debug('post_remap')

    return 0
end

function pre_remap()
    ts.debug('pre_remap')

    return 0
end

function os_dns()
    ts.debug('os_dns')

    return 0
end

function cache_lookup()
    ts.debug('cache_lookup_complete')

    return 0
end

function select_alt()
    ts.debug('select_alt')

    return 0
end

function read_cache()
    ts.debug('read_cache')

    return 0
end

function txn_close()
    ts.debug('txn_close')

    return 0
end

