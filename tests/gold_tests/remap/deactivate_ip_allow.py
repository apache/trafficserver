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
    [  0,  "ip_and_method",  "",                          "", False, ALLOW_GET_AND_POST, 200, 200,   ],
    [  1,  "ip_and_method",  "",                          "", False, ALLOW_GET,          200, 403,   ],
    [  2,  "ip_and_method",  "",                          "", False, DENY_GET,           403, 200,   ],
    [  3,  "ip_and_method",  "",                          "", False, DENY_GET_AND_POST,  403, 403,   ],
    [  4,  "ip_and_method",  "",                          "", False, DENY_ALL,           None, None, ],
    [  5,  "ip_and_method",  "",                          "", True,  ALLOW_GET_AND_POST, 200, 200,   ],
    [  6,  "ip_and_method",  "",                          "", True,  ALLOW_GET,          200, 200,   ],
    [  7,  "ip_and_method",  "",                          "", True,  DENY_GET,           200, 200,   ],
    [  8,  "ip_and_method",  "",                          "", True,  DENY_GET_AND_POST,  200, 200,   ],
    [  9,  "ip_and_method",  "",                          "", True,  DENY_ALL,           200, 200,   ],
    [ 10,  "ip_and_method",  "@action=allow @method=GET", "", False, ALLOW_GET_AND_POST, 200, 200,   ],
    [ 11,  "ip_and_method",  "@action=allow @method=GET", "", False, ALLOW_GET,          200, 403,   ],
    [ 12,  "ip_and_method",  "@action=allow @method=GET", "", False, DENY_GET,           200, 200,   ],
    [ 13,  "ip_and_method",  "@action=allow @method=GET", "", False, DENY_GET_AND_POST,  200, 403,   ],
    [ 14,  "ip_and_method",  "@action=allow @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 15,  "ip_and_method",  "@action=allow @method=GET", "", True,  ALLOW_GET_AND_POST, 200, 200,   ],
    [ 16,  "ip_and_method",  "@action=allow @method=GET", "", True,  ALLOW_GET,          200, 200,   ],
    [ 17,  "ip_and_method",  "@action=allow @method=GET", "", True,  DENY_GET,           200, 200,   ],
    [ 18,  "ip_and_method",  "@action=allow @method=GET", "", True,  DENY_GET_AND_POST,  200, 200,   ],
    [ 19,  "ip_and_method",  "@action=allow @method=GET", "", True,  DENY_ALL,           200, 200,   ],
    [ 20,  "ip_and_method",  "@action=deny  @method=GET", "", False, ALLOW_GET_AND_POST, 403, 200,   ],
    [ 21,  "ip_and_method",  "@action=deny  @method=GET", "", False, ALLOW_GET,          403, 403,   ],
    [ 22,  "ip_and_method",  "@action=deny  @method=GET", "", False, DENY_GET,           403, 200,   ],
    [ 23,  "ip_and_method",  "@action=deny  @method=GET", "", False, DENY_GET_AND_POST,  403, 403,   ],
    [ 24,  "ip_and_method",  "@action=deny  @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 25,  "ip_and_method",  "@action=deny  @method=GET", "", True,  ALLOW_GET_AND_POST, 403, 200,   ],
    [ 26,  "ip_and_method",  "@action=deny  @method=GET", "", True,  ALLOW_GET,          403, 200,   ],
    [ 27,  "ip_and_method",  "@action=deny  @method=GET", "", True,  DENY_GET,           403, 200,   ],
    [ 28,  "ip_and_method",  "@action=deny  @method=GET", "", True,  DENY_GET_AND_POST,  403, 200,   ],
    [ 29,  "ip_and_method",  "@action=deny  @method=GET", "", True,  DENY_ALL,           403, 200,   ],

    # Verify in ip_and_method mode that add_allow acts just like allow, and add_deny acts just like deny.
    [ 30,  "ip_and_method",  "@action=add_allow @method=GET", "", False, ALLOW_GET_AND_POST, 200, 200,   ],
    [ 31,  "ip_and_method",  "@action=add_allow @method=GET", "", False, ALLOW_GET,          200, 403,   ],
    [ 32,  "ip_and_method",  "@action=add_allow @method=GET", "", False, DENY_GET,           200, 200,   ],
    [ 33,  "ip_and_method",  "@action=add_allow @method=GET", "", False, DENY_GET_AND_POST,  200, 403,   ],
    [ 34,  "ip_and_method",  "@action=add_allow @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 35,  "ip_and_method",  "@action=add_allow @method=GET", "", True,  ALLOW_GET_AND_POST, 200, 200,   ],
    [ 36,  "ip_and_method",  "@action=add_allow @method=GET", "", True,  ALLOW_GET,          200, 200,   ],
    [ 37,  "ip_and_method",  "@action=add_allow @method=GET", "", True,  DENY_GET,           200, 200,   ],
    [ 38,  "ip_and_method",  "@action=add_allow @method=GET", "", True,  DENY_GET_AND_POST,  200, 200,   ],
    [ 39,  "ip_and_method",  "@action=add_allow @method=GET", "", True,  DENY_ALL,           200, 200,   ],
    [ 40,  "ip_and_method",  "@action=add_deny  @method=GET", "", False, ALLOW_GET_AND_POST, 403, 200,   ],
    [ 41,  "ip_and_method",  "@action=add_deny  @method=GET", "", False, ALLOW_GET,          403, 403,   ],
    [ 42,  "ip_and_method",  "@action=add_deny  @method=GET", "", False, DENY_GET,           403, 200,   ],
    [ 43,  "ip_and_method",  "@action=add_deny  @method=GET", "", False, DENY_GET_AND_POST,  403, 403,   ],
    [ 44,  "ip_and_method",  "@action=add_deny  @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 45,  "ip_and_method",  "@action=add_deny  @method=GET", "", True,  ALLOW_GET_AND_POST, 403, 200,   ],
    [ 46,  "ip_and_method",  "@action=add_deny  @method=GET", "", True,  ALLOW_GET,          403, 200,   ],
    [ 47,  "ip_and_method",  "@action=add_deny  @method=GET", "", True,  DENY_GET,           403, 200,   ],
    [ 48,  "ip_and_method",  "@action=add_deny  @method=GET", "", True,  DENY_GET_AND_POST,  403, 200,   ],
    [ 49,  "ip_and_method",  "@action=add_deny  @method=GET", "", True,  DENY_ALL,           403, 200,   ],
]
all_deactivate_ip_allow_tests = [dict(zip(keys, test)) for test in deactivate_ip_allow_combinations]
# yapf: enable
