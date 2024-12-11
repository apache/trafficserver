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
    [  0,  "legacy",       "",                             "",                               ALLOW_GET_AND_POST, 200, 200, ],
    [  1,  "legacy",       "",                             "",                               ALLOW_GET,          200, 403, ],
    [  2,  "legacy",       "",                             "",                               DENY_GET,           403, 200, ],
    [  3,  "legacy",       "",                             "",                               DENY_GET_AND_POST,  403, 403, ],
    [  4,  "legacy",       "",                             "@action=allow @method=GET",      ALLOW_GET_AND_POST, 200, 403, ],
    [  5,  "legacy",       "",                             "@action=allow @method=GET",      ALLOW_GET,          200, 403, ],
    [  6,  "legacy",       "",                             "@action=allow @method=GET",      DENY_GET,           200, 403, ],
    [  7,  "legacy",       "",                             "@action=allow @method=GET",      DENY_GET_AND_POST,  200, 403, ],
    [  8,  "legacy",       "",                             "@action=deny  @method=GET",      ALLOW_GET_AND_POST, 403, 200, ],
    [  9,  "legacy",       "",                             "@action=deny  @method=GET",      ALLOW_GET,          403, 200, ],
    [ 10,  "legacy",       "",                             "@action=deny  @method=GET",      DENY_GET,           403, 200, ],
    [ 11,  "legacy",       "",                             "@action=deny  @method=GET",      DENY_GET_AND_POST,  403, 200, ],
    [ 12,  "legacy",       "@action=allow @method=GET",    "",                               ALLOW_GET_AND_POST, 200, 403, ],
    [ 13,  "legacy",       "@action=allow @method=GET",    "",                               ALLOW_GET,          200, 403, ],
    [ 14,  "legacy",       "@action=allow @method=GET",    "",                               DENY_GET,           200, 403, ],
    [ 15,  "legacy",       "@action=allow @method=GET",    "",                               DENY_GET_AND_POST,  200, 403, ],
    [ 16,  "legacy",       "@action=allow @method=GET",    "@action=allow @method=GET",      ALLOW_GET_AND_POST, 200, 403, ],
    [ 17,  "legacy",       "@action=allow @method=GET",    "@action=allow @method=GET",      ALLOW_GET,          200, 403, ],
    [ 18,  "legacy",       "@action=allow @method=GET",    "@action=allow @method=GET",      DENY_GET,           200, 403, ],
    [ 19,  "legacy",       "@action=allow @method=GET",    "@action=allow @method=GET",      DENY_GET_AND_POST,  200, 403, ],
    [ 20,  "legacy",       "@action=allow @method=GET",    "@action=deny  @method=GET",      ALLOW_GET_AND_POST, 403, 403, ],
    [ 21,  "legacy",       "@action=allow @method=GET",    "@action=deny  @method=GET",      ALLOW_GET,          403, 403, ],
    [ 22,  "legacy",       "@action=allow @method=GET",    "@action=deny  @method=GET",      DENY_GET,           403, 403, ],
    [ 23,  "legacy",       "@action=allow @method=GET",    "@action=deny  @method=GET",      DENY_GET_AND_POST,  403, 403, ],
    [ 24,  "legacy",       "@action=allow @method=GET",    "@action=allow @method=POST",     ALLOW_GET_AND_POST, 403, 403, ],
    [ 25,  "legacy",       "@action=allow @method=GET",    "@action=allow @method=POST",     ALLOW_GET,          403, 403, ],
    [ 26,  "legacy",       "@action=allow @method=GET",    "@action=allow @method=POST",     DENY_GET,           403, 403, ],
    [ 27,  "legacy",       "@action=allow @method=GET",    "@action=allow @method=POST",     DENY_GET_AND_POST,  403, 403, ],
    [ 28,  "legacy",       "@action=allow @method=GET",    "@action=deny  @method=POST",     ALLOW_GET_AND_POST, 200, 403, ],
    [ 29,  "legacy",       "@action=allow @method=GET",    "@action=deny  @method=POST",     ALLOW_GET,          200, 403, ],
    [ 30,  "legacy",       "@action=allow @method=GET",    "@action=deny  @method=POST",     DENY_GET,           200, 403, ],
    [ 31,  "legacy",       "@action=allow @method=GET",    "@action=deny  @method=POST",     DENY_GET_AND_POST,  200, 403, ],
    [ 32,  "legacy",       "@action=deny  @method=GET",    "",                               ALLOW_GET_AND_POST, 403, 200, ],
    [ 33,  "legacy",       "@action=deny  @method=GET",    "",                               ALLOW_GET,          403, 200, ],
    [ 34,  "legacy",       "@action=deny  @method=GET",    "",                               DENY_GET,           403, 200, ],
    [ 35,  "legacy",       "@action=deny  @method=GET",    "",                               DENY_GET_AND_POST,  403, 200, ],
    [ 36,  "legacy",       "@action=deny  @method=GET",    "@action=allow @method=GET",      ALLOW_GET_AND_POST, 403, 403, ],
    [ 37,  "legacy",       "@action=deny  @method=GET",    "@action=allow @method=GET",      ALLOW_GET,          403, 403, ],
    [ 38,  "legacy",       "@action=deny  @method=GET",    "@action=allow @method=GET",      DENY_GET,           403, 403, ],
    [ 39,  "legacy",       "@action=deny  @method=GET",    "@action=allow @method=GET",      DENY_GET_AND_POST,  403, 403, ],
    [ 40,  "legacy",       "@action=deny  @method=GET",    "@action=deny  @method=GET",      ALLOW_GET_AND_POST, 403, 200, ],
    [ 41,  "legacy",       "@action=deny  @method=GET",    "@action=deny  @method=GET",      ALLOW_GET,          403, 200, ],
    [ 42,  "legacy",       "@action=deny  @method=GET",    "@action=deny  @method=GET",      DENY_GET,           403, 200, ],
    [ 43,  "legacy",       "@action=deny  @method=GET",    "@action=deny  @method=GET",      DENY_GET_AND_POST,  403, 200, ],
    [ 44,  "legacy",       "@action=deny  @method=GET",    "@action=allow @method=POST",     ALLOW_GET_AND_POST, 403, 200, ],
    [ 45,  "legacy",       "@action=deny  @method=GET",    "@action=allow @method=POST",     ALLOW_GET,          403, 200, ],
    [ 46,  "legacy",       "@action=deny  @method=GET",    "@action=allow @method=POST",     DENY_GET,           403, 200, ],
    [ 47,  "legacy",       "@action=deny  @method=GET",    "@action=allow @method=POST",     DENY_GET_AND_POST,  403, 200, ],
    [ 48,  "legacy",       "@action=deny  @method=GET",    "@action=deny  @method=POST",     ALLOW_GET_AND_POST, 403, 403, ],
    [ 49,  "legacy",       "@action=deny  @method=GET",    "@action=deny  @method=POST",     ALLOW_GET,          403, 403, ],
    [ 50,  "legacy",       "@action=deny  @method=GET",    "@action=deny  @method=POST",     DENY_GET,           403, 403, ],
    [ 51,  "legacy",       "@action=deny  @method=GET",    "@action=deny  @method=POST",     DENY_GET_AND_POST,  403, 403, ],
    [ 52,  "modern",       "",                              "",                              ALLOW_GET_AND_POST, 200, 200, ],
    [ 53,  "modern",       "",                              "",                              ALLOW_GET,          200, 403, ],
    [ 54,  "modern",       "",                              "",                              DENY_GET,           403, 200, ],
    [ 55,  "modern",       "",                              "",                              DENY_GET_AND_POST,  403, 403, ],
    [ 56,  "modern",       "",                              "@action=set_allow @method=GET", ALLOW_GET_AND_POST, 200, 403, ],
    [ 57,  "modern",       "",                              "@action=set_allow @method=GET", ALLOW_GET,          200, 403, ],
    [ 58,  "modern",       "",                              "@action=set_allow @method=GET", DENY_GET,           200, 403, ],
    [ 59,  "modern",       "",                              "@action=set_allow @method=GET", DENY_GET_AND_POST,  200, 403, ],
    [ 60,  "modern",       "",                              "@action=set_deny  @method=GET", ALLOW_GET_AND_POST, 403, 200, ],
    [ 61,  "modern",       "",                              "@action=set_deny  @method=GET", ALLOW_GET,          403, 200, ],
    [ 62,  "modern",       "",                              "@action=set_deny  @method=GET", DENY_GET,           403, 200, ],
    [ 63,  "modern",       "",                              "@action=set_deny  @method=GET", DENY_GET_AND_POST,  403, 200, ],
    [ 64,  "modern",       "@action=set_allow @method=GET", "",                              ALLOW_GET_AND_POST, 200, 403, ],
    [ 65,  "modern",       "@action=set_allow @method=GET", "",                              ALLOW_GET,          200, 403, ],
    [ 66,  "modern",       "@action=set_allow @method=GET", "",                              DENY_GET,           200, 403, ],
    [ 67,  "modern",       "@action=set_allow @method=GET", "",                              DENY_GET_AND_POST,  200, 403, ],
    [ 68,  "modern",       "@action=set_allow @method=GET", "@action=set_allow @method=GET", ALLOW_GET_AND_POST, 200, 403, ],
    [ 69,  "modern",       "@action=set_allow @method=GET", "@action=set_allow @method=GET", ALLOW_GET,          200, 403, ],
    [ 70,  "modern",       "@action=set_allow @method=GET", "@action=set_allow @method=GET", DENY_GET,           200, 403, ],
    [ 71,  "modern",       "@action=set_allow @method=GET", "@action=set_allow @method=GET", DENY_GET_AND_POST,  200, 403, ],
    [ 72,  "modern",       "@action=set_allow @method=GET", "@action=set_deny  @method=GET", ALLOW_GET_AND_POST, 200, 403, ],
    [ 73,  "modern",       "@action=set_allow @method=GET", "@action=set_deny  @method=GET", ALLOW_GET,          200, 403, ],
    [ 74,  "modern",       "@action=set_allow @method=GET", "@action=set_deny  @method=GET", DENY_GET,           200, 403, ],
    [ 75,  "modern",       "@action=set_allow @method=GET", "@action=set_deny  @method=GET", DENY_GET_AND_POST,  200, 403, ],
    [ 76,  "modern",       "@action=set_deny  @method=GET", "",                              ALLOW_GET_AND_POST, 403, 200, ],
    [ 77,  "modern",       "@action=set_deny  @method=GET", "",                              ALLOW_GET,          403, 200, ],
    [ 78,  "modern",       "@action=set_deny  @method=GET", "",                              DENY_GET,           403, 200, ],
    [ 79,  "modern",       "@action=set_deny  @method=GET", "",                              DENY_GET_AND_POST,  403, 200, ],
    [ 80,  "modern",       "@action=set_deny  @method=GET", "@action=set_allow @method=GET", ALLOW_GET_AND_POST, 403, 200, ],
    [ 81,  "modern",       "@action=set_deny  @method=GET", "@action=set_allow @method=GET", ALLOW_GET,          403, 200, ],
    [ 82,  "modern",       "@action=set_deny  @method=GET", "@action=set_allow @method=GET", DENY_GET,           403, 200, ],
    [ 83,  "modern",       "@action=set_deny  @method=GET", "@action=set_allow @method=GET", DENY_GET_AND_POST,  403, 200, ],
    [ 84,  "modern",       "@action=set_deny  @method=GET", "@action=set_deny  @method=GET", ALLOW_GET_AND_POST, 403, 200, ],
    [ 85,  "modern",       "@action=set_deny  @method=GET", "@action=set_deny  @method=GET", ALLOW_GET,          403, 200, ],
    [ 86,  "modern",       "@action=set_deny  @method=GET", "@action=set_deny  @method=GET", DENY_GET,           403, 200, ],
    [ 87,  "modern",       "@action=set_deny  @method=GET", "@action=set_deny  @method=GET", DENY_GET_AND_POST,  403, 200, ],
    [ 88,  "legacy",       "@src_ip=1.2.3.0-1.2.3.255 @action=allow",    "",                 ALLOW_GET_AND_POST, 403, 403, ],
]
# yapf: enable

all_acl_combination_tests = [dict(zip(keys, test)) for test in all_acl_combinations]
