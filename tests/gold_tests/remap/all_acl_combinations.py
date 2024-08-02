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
    action: set_allow
    methods: [GET, POST]
'''

ALLOW_GET = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: set_allow
    methods: [GET]
'''

DENY_GET = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: set_deny
    methods: [GET]
'''

DENY_GET_AND_POST = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: set_deny
    methods: [GET, POST]
'''

# yapf: disable
keys = ["index", "policy", "inline", "named_acl", "ip_allow", "GET response", "POST response"]
all_acl_combinations = [
    [  0,  "ip_and_method",  "",                             "",                              ALLOW_GET_AND_POST, 200, 200, ],
    [  1,  "ip_and_method",  "",                             "",                              ALLOW_GET,          200, 403, ],
    [  2,  "ip_and_method",  "",                             "",                              DENY_GET,           403, 200, ],
    [  3,  "ip_and_method",  "",                             "",                              DENY_GET_AND_POST,  403, 403, ],
    [  4,  "ip_and_method",  "",                             "@action=allow @method=GET",     ALLOW_GET_AND_POST, 200, 200, ],
    [  5,  "ip_and_method",  "",                             "@action=allow @method=GET",     ALLOW_GET,          200, 403, ],
    [  6,  "ip_and_method",  "",                             "@action=allow @method=GET",     DENY_GET,           200, 200, ],
    [  7,  "ip_and_method",  "",                             "@action=allow @method=GET",     DENY_GET_AND_POST,  200, 403, ],
    [  8,  "ip_and_method",  "",                             "@action=deny  @method=GET",     ALLOW_GET_AND_POST, 403, 200, ],
    [  9,  "ip_and_method",  "",                             "@action=deny  @method=GET",     ALLOW_GET,          403, 403, ],
    [ 10,  "ip_and_method",  "",                             "@action=deny  @method=GET",     DENY_GET,           403, 200, ],
    [ 11,  "ip_and_method",  "",                             "@action=deny  @method=GET",     DENY_GET_AND_POST,  403, 403, ],
    [ 12,  "ip_and_method",  "@action=allow @method=GET",    "",                              ALLOW_GET_AND_POST, 200, 200, ],
    [ 13,  "ip_and_method",  "@action=allow @method=GET",    "",                              ALLOW_GET,          200, 403, ],
    [ 14,  "ip_and_method",  "@action=allow @method=GET",    "",                              DENY_GET,           200, 200, ],
    [ 15,  "ip_and_method",  "@action=allow @method=GET",    "",                              DENY_GET_AND_POST,  200, 403, ],
    [ 16,  "ip_and_method",  "@action=allow @method=GET",    "@action=allow @method=GET",     ALLOW_GET_AND_POST, 200, 200, ],
    [ 17,  "ip_and_method",  "@action=allow @method=GET",    "@action=allow @method=GET",     ALLOW_GET,          200, 403, ],
    [ 18,  "ip_and_method",  "@action=allow @method=GET",    "@action=allow @method=GET",     DENY_GET,           200, 200, ],
    [ 19,  "ip_and_method",  "@action=allow @method=GET",    "@action=allow @method=GET",     DENY_GET_AND_POST,  200, 403, ],
    [ 20,  "ip_and_method",  "@action=allow @method=GET",    "@action=deny  @method=GET",     ALLOW_GET_AND_POST, 200, 200, ],
    [ 21,  "ip_and_method",  "@action=allow @method=GET",    "@action=deny  @method=GET",     ALLOW_GET,          200, 403, ],
    [ 22,  "ip_and_method",  "@action=allow @method=GET",    "@action=deny  @method=GET",     DENY_GET,           200, 200, ],
    [ 23,  "ip_and_method",  "@action=allow @method=GET",    "@action=deny  @method=GET",     DENY_GET_AND_POST,  200, 403, ],
    [ 24,  "ip_and_method",  "@action=allow @method=GET",    "@action=allow @method=POST",    ALLOW_GET_AND_POST, 200, 200, ],
    [ 25,  "ip_and_method",  "@action=allow @method=GET",    "@action=allow @method=POST",    ALLOW_GET,          200, 200, ],
    [ 26,  "ip_and_method",  "@action=allow @method=GET",    "@action=allow @method=POST",    DENY_GET,           200, 200, ],
    [ 27,  "ip_and_method",  "@action=allow @method=GET",    "@action=allow @method=POST",    DENY_GET_AND_POST,  200, 200, ],
    [ 28,  "ip_and_method",  "@action=allow @method=GET",    "@action=deny  @method=POST",    ALLOW_GET_AND_POST, 200, 403, ],
    [ 29,  "ip_and_method",  "@action=allow @method=GET",    "@action=deny  @method=POST",    ALLOW_GET,          200, 403, ],
    [ 30,  "ip_and_method",  "@action=allow @method=GET",    "@action=deny  @method=POST",    DENY_GET,           200, 403, ],
    [ 31,  "ip_and_method",  "@action=allow @method=GET",    "@action=deny  @method=POST",    DENY_GET_AND_POST,  200, 403, ],
    [ 32,  "ip_and_method",  "@action=deny  @method=GET",    "",                              ALLOW_GET_AND_POST, 403, 200, ],
    [ 33,  "ip_and_method",  "@action=deny  @method=GET",    "",                              ALLOW_GET,          403, 403, ],
    [ 34,  "ip_and_method",  "@action=deny  @method=GET",    "",                              DENY_GET,           403, 200, ],
    [ 35,  "ip_and_method",  "@action=deny  @method=GET",    "",                              DENY_GET_AND_POST,  403, 403, ],
    [ 36,  "ip_and_method",  "@action=deny  @method=GET",    "@action=allow @method=GET",     ALLOW_GET_AND_POST, 403, 200, ],
    [ 37,  "ip_and_method",  "@action=deny  @method=GET",    "@action=allow @method=GET",     ALLOW_GET,          403, 403, ],
    [ 38,  "ip_and_method",  "@action=deny  @method=GET",    "@action=allow @method=GET",     DENY_GET,           403, 200, ],
    [ 39,  "ip_and_method",  "@action=deny  @method=GET",    "@action=allow @method=GET",     DENY_GET_AND_POST,  403, 403, ],
    [ 40,  "ip_and_method",  "@action=deny  @method=GET",    "@action=deny  @method=GET",     ALLOW_GET_AND_POST, 403, 200, ],
    [ 41,  "ip_and_method",  "@action=deny  @method=GET",    "@action=deny  @method=GET",     ALLOW_GET,          403, 403, ],
    [ 42,  "ip_and_method",  "@action=deny  @method=GET",    "@action=deny  @method=GET",     DENY_GET,           403, 200, ],
    [ 43,  "ip_and_method",  "@action=deny  @method=GET",    "@action=deny  @method=GET",     DENY_GET_AND_POST,  403, 403, ],
    [ 44,  "ip_and_method",  "@action=deny  @method=GET",    "@action=allow @method=POST",    ALLOW_GET_AND_POST, 403, 200, ],
    [ 45,  "ip_and_method",  "@action=deny  @method=GET",    "@action=allow @method=POST",    ALLOW_GET,          403, 200, ],
    [ 46,  "ip_and_method",  "@action=deny  @method=GET",    "@action=allow @method=POST",    DENY_GET,           403, 200, ],
    [ 47,  "ip_and_method",  "@action=deny  @method=GET",    "@action=allow @method=POST",    DENY_GET_AND_POST,  403, 200, ],
    [ 48,  "ip_and_method",  "@action=deny  @method=GET",    "@action=deny  @method=POST",    ALLOW_GET_AND_POST, 403, 403, ],
    [ 49,  "ip_and_method",  "@action=deny  @method=GET",    "@action=deny  @method=POST",    ALLOW_GET,          403, 403, ],
    [ 50,  "ip_and_method",  "@action=deny  @method=GET",    "@action=deny  @method=POST",    DENY_GET,           403, 403, ],
    [ 51,  "ip_and_method",  "@action=deny  @method=GET",    "@action=deny  @method=POST",    DENY_GET_AND_POST,  403, 403, ],
    [ 52,  "ip_only",       "",                              "",                              ALLOW_GET_AND_POST, 200, 200, ],
    [ 53,  "ip_only",       "",                              "",                              ALLOW_GET,          200, 403, ],
    [ 54,  "ip_only",       "",                              "",                              DENY_GET,           403, 200, ],
    [ 55,  "ip_only",       "",                              "",                              DENY_GET_AND_POST,  403, 403, ],
    [ 56,  "ip_only",       "",                              "@action=set_allow @method=GET", ALLOW_GET_AND_POST, 200, 403, ],
    [ 57,  "ip_only",       "",                              "@action=set_allow @method=GET", ALLOW_GET,          200, 403, ],
    [ 58,  "ip_only",       "",                              "@action=set_allow @method=GET", DENY_GET,           200, 403, ],
    [ 59,  "ip_only",       "",                              "@action=set_allow @method=GET", DENY_GET_AND_POST,  200, 403, ],
    [ 60,  "ip_only",       "",                              "@action=set_deny  @method=GET", ALLOW_GET_AND_POST, 403, 200, ],
    [ 61,  "ip_only",       "",                              "@action=set_deny  @method=GET", ALLOW_GET,          403, 200, ],
    [ 62,  "ip_only",       "",                              "@action=set_deny  @method=GET", DENY_GET,           403, 200, ],
    [ 63,  "ip_only",       "",                              "@action=set_deny  @method=GET", DENY_GET_AND_POST,  403, 200, ],
    [ 64,  "ip_only",       "@action=set_allow @method=GET", "",                              ALLOW_GET_AND_POST, 200, 403, ],
    [ 65,  "ip_only",       "@action=set_allow @method=GET", "",                              ALLOW_GET,          200, 403, ],
    [ 66,  "ip_only",       "@action=set_allow @method=GET", "",                              DENY_GET,           200, 403, ],
    [ 67,  "ip_only",       "@action=set_allow @method=GET", "",                              DENY_GET_AND_POST,  200, 403, ],
    [ 68,  "ip_only",       "@action=set_allow @method=GET", "@action=set_allow @method=GET", ALLOW_GET_AND_POST, 200, 403, ],
    [ 69,  "ip_only",       "@action=set_allow @method=GET", "@action=set_allow @method=GET", ALLOW_GET,          200, 403, ],
    [ 70,  "ip_only",       "@action=set_allow @method=GET", "@action=set_allow @method=GET", DENY_GET,           200, 403, ],
    [ 71,  "ip_only",       "@action=set_allow @method=GET", "@action=set_allow @method=GET", DENY_GET_AND_POST,  200, 403, ],
    [ 72,  "ip_only",       "@action=set_allow @method=GET", "@action=set_deny  @method=GET", ALLOW_GET_AND_POST, 200, 403, ],
    [ 73,  "ip_only",       "@action=set_allow @method=GET", "@action=set_deny  @method=GET", ALLOW_GET,          200, 403, ],
    [ 74,  "ip_only",       "@action=set_allow @method=GET", "@action=set_deny  @method=GET", DENY_GET,           200, 403, ],
    [ 75,  "ip_only",       "@action=set_allow @method=GET", "@action=set_deny  @method=GET", DENY_GET_AND_POST,  200, 403, ],
    [ 76,  "ip_only",       "@action=set_deny  @method=GET", "",                              ALLOW_GET_AND_POST, 403, 200, ],
    [ 77,  "ip_only",       "@action=set_deny  @method=GET", "",                              ALLOW_GET,          403, 200, ],
    [ 78,  "ip_only",       "@action=set_deny  @method=GET", "",                              DENY_GET,           403, 200, ],
    [ 79,  "ip_only",       "@action=set_deny  @method=GET", "",                              DENY_GET_AND_POST,  403, 200, ],
    [ 80,  "ip_only",       "@action=set_deny  @method=GET", "@action=set_allow @method=GET", ALLOW_GET_AND_POST, 403, 200, ],
    [ 81,  "ip_only",       "@action=set_deny  @method=GET", "@action=set_allow @method=GET", ALLOW_GET,          403, 200, ],
    [ 82,  "ip_only",       "@action=set_deny  @method=GET", "@action=set_allow @method=GET", DENY_GET,           403, 200, ],
    [ 83,  "ip_only",       "@action=set_deny  @method=GET", "@action=set_allow @method=GET", DENY_GET_AND_POST,  403, 200, ],
    [ 84,  "ip_only",       "@action=set_deny  @method=GET", "@action=set_deny  @method=GET", ALLOW_GET_AND_POST, 403, 200, ],
    [ 85,  "ip_only",       "@action=set_deny  @method=GET", "@action=set_deny  @method=GET", ALLOW_GET,          403, 200, ],
    [ 86,  "ip_only",       "@action=set_deny  @method=GET", "@action=set_deny  @method=GET", DENY_GET,           403, 200, ],
    [ 87,  "ip_only",       "@action=set_deny  @method=GET", "@action=set_deny  @method=GET", DENY_GET_AND_POST,  403, 200, ],
]
# yapf: enable

all_acl_combination_tests = [dict(zip(keys, test)) for test in all_acl_combinations]
