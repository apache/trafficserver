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

function do_global_vconn_start()
    ts.debug('vconn_start')

    ip, port, family = ts.vconn.get_remote_addr()
    ts.debug('vconn: '..ip)               -- 192.168.231.17
    ts.debug('vconn: '..port)             -- 17786
    ts.debug('vconn: '..family)           -- 2(AF_INET)

    fd = ts.vconn.get_fd()
    ts.debug('vconn: '..fd)

    ts.vconn.disable_h2()
end

function do_global_txn_close()
    ts.debug('txn_close')

    ip, port, family = ts.http.get_ssn_remote_addr()
    ts.debug('txn_close: '..ip)               -- 192.168.231.17
    ts.debug('txn_close: '..port)             -- 17786
    ts.debug('txn_close: '..family)           -- 2(AF_INET)

    class, code = ts.http.get_client_received_error()
    ts.debug('txn_close: '..class)
    ts.debug('txn_close: '..code)
end
