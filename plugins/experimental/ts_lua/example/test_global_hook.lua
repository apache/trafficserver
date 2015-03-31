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
    ts.debug('txn_start')

    return 0
end 

function do_global_read_request()
    ts.debug('read_request')

    return 0
end

function do_global_send_request()
    ts.debug('send_request')

    return 0
end

function do_global_read_response()
    ts.debug('read_response')

    return 0
end

function do_global_send_response()
    ts.debug('send_response')

    return 0
end

function do_global_post_remap()
    ts.debug('post_remap')

    return 0
end

function do_global_pre_remap()
    ts.debug('pre_remap')

    return 0
end

function do_global_os_dns()
    ts.debug('os_dns')

    return 0
end

function do_global_cache_lookup_complete()
    ts.debug('cache_lookup_complete')
  
    return 0
end

--function do_global_select_alt()
--    ts.debug('select_alt')
--
--    return 0
--end

function do_global_read_cache()
    ts.debug('read_cache')

    return 0
end

function do_global_txn_close()
    ts.debug('txn_close')

    return 0
end

