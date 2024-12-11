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
keys = ["index", "policy", "inline", "named_acl", "deactivate_ip_allow", "ip_allow", "GET response", "POST response"]
deactivate_ip_allow_combinations = [
    [  0,  "legacy",  "",                          "", False, ALLOW_GET_AND_POST, 200, 200,   ],
    [  1,  "legacy",  "",                          "", False, ALLOW_GET,          200, 403,   ],
    [  2,  "legacy",  "",                          "", False, DENY_GET,           403, 200,   ],
    [  3,  "legacy",  "",                          "", False, DENY_GET_AND_POST,  403, 403,   ],
    [  4,  "legacy",  "",                          "", False, DENY_ALL,           None, None, ],
    [  5,  "legacy",  "",                          "", True,  ALLOW_GET_AND_POST, 200, 200,   ],
    [  6,  "legacy",  "",                          "", True,  ALLOW_GET,          200, 200,   ],
    [  7,  "legacy",  "",                          "", True,  DENY_GET,           200, 200,   ],
    [  8,  "legacy",  "",                          "", True,  DENY_GET_AND_POST,  200, 200,   ],
    [  9,  "legacy",  "",                          "", True,  DENY_ALL,           200, 200,   ],
    [ 10,  "legacy",  "@action=allow @method=GET", "", False, ALLOW_GET_AND_POST, 200, 403,   ],
    [ 11,  "legacy",  "@action=allow @method=GET", "", False, ALLOW_GET,          200, 403,   ],
    [ 12,  "legacy",  "@action=allow @method=GET", "", False, DENY_GET,           200, 403,   ],
    [ 13,  "legacy",  "@action=allow @method=GET", "", False, DENY_GET_AND_POST,  200, 403,   ],
    [ 14,  "legacy",  "@action=allow @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 15,  "legacy",  "@action=allow @method=GET", "", True,  ALLOW_GET_AND_POST, 200, 403,   ],
    [ 16,  "legacy",  "@action=allow @method=GET", "", True,  ALLOW_GET,          200, 403,   ],
    [ 17,  "legacy",  "@action=allow @method=GET", "", True,  DENY_GET,           200, 403,   ],
    [ 18,  "legacy",  "@action=allow @method=GET", "", True,  DENY_GET_AND_POST,  200, 403,   ],
    [ 19,  "legacy",  "@action=allow @method=GET", "", True,  DENY_ALL,           200, 403,   ],
    [ 20,  "legacy",  "@action=deny  @method=GET", "", False, ALLOW_GET_AND_POST, 403, 200,   ],
    [ 21,  "legacy",  "@action=deny  @method=GET", "", False, ALLOW_GET,          403, 200,   ],
    [ 22,  "legacy",  "@action=deny  @method=GET", "", False, DENY_GET,           403, 200,   ],
    [ 23,  "legacy",  "@action=deny  @method=GET", "", False, DENY_GET_AND_POST,  403, 200,   ],
    [ 24,  "legacy",  "@action=deny  @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 25,  "legacy",  "@action=deny  @method=GET", "", True,  ALLOW_GET_AND_POST, 403, 200,   ],
    [ 26,  "legacy",  "@action=deny  @method=GET", "", True,  ALLOW_GET,          403, 200,   ],
    [ 27,  "legacy",  "@action=deny  @method=GET", "", True,  DENY_GET,           403, 200,   ],
    [ 28,  "legacy",  "@action=deny  @method=GET", "", True,  DENY_GET_AND_POST,  403, 200,   ],
    [ 29,  "legacy",  "@action=deny  @method=GET", "", True,  DENY_ALL,           403, 200,   ],

    # Verify in legacy mode that add_allow acts just like allow, and add_deny acts just like deny.
    [ 30,  "legacy",  "@action=add_allow @method=GET", "", False, ALLOW_GET_AND_POST, 200, 403,   ],
    [ 31,  "legacy",  "@action=add_allow @method=GET", "", False, ALLOW_GET,          200, 403,   ],
    [ 32,  "legacy",  "@action=add_allow @method=GET", "", False, DENY_GET,           200, 403,   ],
    [ 33,  "legacy",  "@action=add_allow @method=GET", "", False, DENY_GET_AND_POST,  200, 403,   ],
    [ 34,  "legacy",  "@action=add_allow @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 35,  "legacy",  "@action=add_allow @method=GET", "", True,  ALLOW_GET_AND_POST, 200, 403,   ],
    [ 36,  "legacy",  "@action=add_allow @method=GET", "", True,  ALLOW_GET,          200, 403,   ],
    [ 37,  "legacy",  "@action=add_allow @method=GET", "", True,  DENY_GET,           200, 403,   ],
    [ 38,  "legacy",  "@action=add_allow @method=GET", "", True,  DENY_GET_AND_POST,  200, 403,   ],
    [ 39,  "legacy",  "@action=add_allow @method=GET", "", True,  DENY_ALL,           200, 403,   ],
    [ 40,  "legacy",  "@action=add_deny  @method=GET", "", False, ALLOW_GET_AND_POST, 403, 200,   ],
    [ 41,  "legacy",  "@action=add_deny  @method=GET", "", False, ALLOW_GET,          403, 200,   ],
    [ 42,  "legacy",  "@action=add_deny  @method=GET", "", False, DENY_GET,           403, 200,   ],
    [ 43,  "legacy",  "@action=add_deny  @method=GET", "", False, DENY_GET_AND_POST,  403, 200,   ],
    [ 44,  "legacy",  "@action=add_deny  @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 45,  "legacy",  "@action=add_deny  @method=GET", "", True,  ALLOW_GET_AND_POST, 403, 200,   ],
    [ 46,  "legacy",  "@action=add_deny  @method=GET", "", True,  ALLOW_GET,          403, 200,   ],
    [ 47,  "legacy",  "@action=add_deny  @method=GET", "", True,  DENY_GET,           403, 200,   ],
    [ 48,  "legacy",  "@action=add_deny  @method=GET", "", True,  DENY_GET_AND_POST,  403, 200,   ],
    [ 49,  "legacy",  "@action=add_deny  @method=GET", "", True,  DENY_ALL,           403, 200,   ],
]
all_deactivate_ip_allow_tests = [dict(zip(keys, test)) for test in deactivate_ip_allow_combinations]
# yapf: enable
