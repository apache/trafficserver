'''
Verify remap.config acl behavior.
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

ALLOW_GET_AND_POST = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: allow
    methods: [GET, POST]
'''

ALLOW_GET = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: allow
    methods: [GET]
'''

DENY_GET = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: deny
    methods: [GET]
'''

DENY_GET_AND_POST = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: deny
    methods: [GET, POST]
'''

# Optimized ACL filter on accept
DENY_ALL = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: deny
    methods: ALL
'''

# yapf: disable
keys = ["index", "inline", "named_acl", "deactivate_ip_allow", "ip_allow", "GET response", "POST response"]
deactivate_ip_allow_combinations = [
    [  0,  "",                                            "", False, ALLOW_GET_AND_POST, 200, 200,   ],
    [  1,  "",                                            "", False, ALLOW_GET,          200, 403,   ],
    [  2,  "",                                            "", False, DENY_GET,           403, 200,   ],
    [  3,  "",                                            "", False, DENY_GET_AND_POST,  403, 403,   ],
    [  4,  "",                                            "", False, DENY_ALL,           None, None, ],
    [  5,  "",                                            "", True,  ALLOW_GET_AND_POST, 200, 200,   ],
    [  6,  "",                                            "", True,  ALLOW_GET,          200, 200,   ],
    [  7,  "",                                            "", True,  DENY_GET,           200, 200,   ],
    [  8,  "",                                            "", True,  DENY_GET_AND_POST,  200, 200,   ],
    [  9,  "",                                            "", True,  DENY_ALL,           200, 200,   ],
    [ 10,  "@action=allow @method=GET",                   "", False, ALLOW_GET_AND_POST, 200, 403,   ],
    [ 11,  "@action=allow @method=GET",                   "", False, ALLOW_GET,          200, 403,   ],
    [ 12,  "@action=allow @method=GET",                   "", False, DENY_GET,           403, 403,   ],
    [ 13,  "@action=allow @method=GET",                   "", False, DENY_GET_AND_POST,  403, 403,   ],
    [ 14,  "@action=allow @method=GET",                   "", False, DENY_ALL,           None, None, ],
    [ 15,  "@action=allow @method=GET",                   "", True,  ALLOW_GET_AND_POST, 200, 403,   ],
    [ 16,  "@action=allow @method=GET",                   "", True,  ALLOW_GET,          200, 403,   ],
    [ 17,  "@action=allow @method=GET",                   "", True,  DENY_GET,           200, 403,   ],
    [ 18,  "@action=allow @method=GET",                   "", True,  DENY_GET_AND_POST,  200, 403,   ],
    [ 19,  "@action=allow @method=GET",                   "", True,  DENY_ALL,           200, 403,   ],
    [ 20,  "@action=deny  @method=GET",                   "", False, ALLOW_GET_AND_POST, 403, 200,   ],
    [ 21,  "@action=deny  @method=GET",                   "", False, ALLOW_GET,          403, 403,   ],
    [ 22,  "@action=deny  @method=GET",                   "", False, DENY_GET,           403, 200,   ],
    [ 23,  "@action=deny  @method=GET",                   "", False, DENY_GET_AND_POST,  403, 403,   ],
    [ 24,  "@action=deny  @method=GET",                   "", False, DENY_ALL,           None, None, ],
    [ 25,  "@action=deny  @method=GET",                   "", True,  ALLOW_GET_AND_POST, 403, 200,   ],
    [ 26,  "@action=deny  @method=GET",                   "", True,  ALLOW_GET,          403, 200,   ],
    [ 27,  "@action=deny  @method=GET",                   "", True,  DENY_GET,           403, 200,   ],
    [ 28,  "@action=deny  @method=GET",                   "", True,  DENY_GET_AND_POST,  403, 200,   ],
    [ 29,  "@action=deny  @method=GET",                   "", True,  DENY_ALL,           403, 200,   ],
    [ 30,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "", False, DENY_ALL,           None, None, ],
    [ 31,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "", True,  DENY_ALL,           200, 200,   ],
    [ 32,  "@action=deny  @src_ip=127.0.0.1 @src_ip=::1", "", False, DENY_ALL,           None, None, ],
    [ 33,  "@action=deny  @src_ip=127.0.0.1 @src_ip=::1", "", True,  DENY_ALL,           403, 403,   ],
    [ 34,  "@action=allow @src_ip=192.0.2.1/24",          "", False, DENY_ALL,           None, None, ],
    [ 35,  "@action=allow @src_ip=192.0.2.1/24",          "", True,  DENY_ALL,           403, 403,   ],
    [ 36,  "@action=deny  @src_ip=192.0.2.0/24",          "", False, DENY_ALL,           None, None, ],
    [ 37,  "@action=deny  @src_ip=192.0.2.0/24",          "", True,  DENY_ALL,           200, 200,   ],
]
all_deactivate_ip_allow_tests = [dict(zip(keys, test)) for test in deactivate_ip_allow_combinations]
# yapf: enable
