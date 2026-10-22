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

# yapf: disable
keys = ["index", "inline", "named_acl", "ip_allow", "GET response", "POST response"]
all_acl_combinations = [
    [  0,  "",                                            "",                                            ALLOW_GET_AND_POST, 200, 200, ],
    [  1,  "",                                            "",                                            ALLOW_GET,          200, 403, ],
    [  2,  "",                                            "",                                            DENY_GET,           403, 200, ],
    [  3,  "",                                            "",                                            DENY_GET_AND_POST,  403, 403, ],
    [  4,  "",                                            "@action=allow @method=GET",                   ALLOW_GET_AND_POST, 200, 403, ],
    [  5,  "",                                            "@action=allow @method=GET",                   ALLOW_GET,          200, 403, ],
    [  6,  "",                                            "@action=allow @method=GET",                   DENY_GET,           403, 403, ],
    [  7,  "",                                            "@action=allow @method=GET",                   DENY_GET_AND_POST,  403, 403, ],
    [  8,  "",                                            "@action=deny  @method=GET",                   ALLOW_GET_AND_POST, 403, 200, ],
    [  9,  "",                                            "@action=deny  @method=GET",                   ALLOW_GET,          403, 403, ],
    [ 10,  "",                                            "@action=deny  @method=GET",                   DENY_GET,           403, 200, ],
    [ 11,  "",                                            "@action=deny  @method=GET",                   DENY_GET_AND_POST,  403, 403, ],
    [ 12,  "@action=allow @method=GET",                   "",                                            ALLOW_GET_AND_POST, 200, 403, ],
    [ 13,  "@action=allow @method=GET",                   "",                                            ALLOW_GET,          200, 403, ],
    [ 14,  "@action=allow @method=GET",                   "",                                            DENY_GET,           403, 403, ],
    [ 15,  "@action=allow @method=GET",                   "",                                            DENY_GET_AND_POST,  403, 403, ],
    [ 16,  "@action=allow @method=GET",                   "@action=allow @method=GET",                   ALLOW_GET_AND_POST, 200, 403, ],
    [ 17,  "@action=allow @method=GET",                   "@action=allow @method=GET",                   ALLOW_GET,          200, 403, ],
    [ 18,  "@action=allow @method=GET",                   "@action=allow @method=GET",                   DENY_GET,           403, 403, ],
    [ 19,  "@action=allow @method=GET",                   "@action=allow @method=GET",                   DENY_GET_AND_POST,  403, 403, ],
    [ 20,  "@action=allow @method=GET",                   "@action=deny  @method=GET",                   ALLOW_GET_AND_POST, 403, 403, ],
    [ 21,  "@action=allow @method=GET",                   "@action=deny  @method=GET",                   ALLOW_GET,          403, 403, ],
    [ 22,  "@action=allow @method=GET",                   "@action=deny  @method=GET",                   DENY_GET,           403, 403, ],
    [ 23,  "@action=allow @method=GET",                   "@action=deny  @method=GET",                   DENY_GET_AND_POST,  403, 403, ],
    [ 24,  "@action=allow @method=GET",                   "@action=allow @method=POST",                  ALLOW_GET_AND_POST, 403, 403, ],
    [ 25,  "@action=allow @method=GET",                   "@action=allow @method=POST",                  ALLOW_GET,          403, 403, ],
    [ 26,  "@action=allow @method=GET",                   "@action=allow @method=POST",                  DENY_GET,           403, 403, ],
    [ 27,  "@action=allow @method=GET",                   "@action=allow @method=POST",                  DENY_GET_AND_POST,  403, 403, ],
    [ 28,  "@action=allow @method=GET",                   "@action=deny  @method=POST",                  ALLOW_GET_AND_POST, 200, 403, ],
    [ 29,  "@action=allow @method=GET",                   "@action=deny  @method=POST",                  ALLOW_GET,          200, 403, ],
    [ 30,  "@action=allow @method=GET",                   "@action=deny  @method=POST",                  DENY_GET,           403, 403, ],
    [ 31,  "@action=allow @method=GET",                   "@action=deny  @method=POST",                  DENY_GET_AND_POST,  403, 403, ],
    [ 32,  "@action=deny  @method=GET",                   "",                                            ALLOW_GET_AND_POST, 403, 200, ],
    [ 33,  "@action=deny  @method=GET",                   "",                                            ALLOW_GET,          403, 403, ],
    [ 34,  "@action=deny  @method=GET",                   "",                                            DENY_GET,           403, 200, ],
    [ 35,  "@action=deny  @method=GET",                   "",                                            DENY_GET_AND_POST,  403, 403, ],
    [ 36,  "@action=deny  @method=GET",                   "@action=allow @method=GET",                   ALLOW_GET_AND_POST, 403, 403, ],
    [ 37,  "@action=deny  @method=GET",                   "@action=allow @method=GET",                   ALLOW_GET,          403, 403, ],
    [ 38,  "@action=deny  @method=GET",                   "@action=allow @method=GET",                   DENY_GET,           403, 403, ],
    [ 39,  "@action=deny  @method=GET",                   "@action=allow @method=GET",                   DENY_GET_AND_POST,  403, 403, ],
    [ 40,  "@action=deny  @method=GET",                   "@action=deny  @method=GET",                   ALLOW_GET_AND_POST, 403, 200, ],
    [ 41,  "@action=deny  @method=GET",                   "@action=deny  @method=GET",                   ALLOW_GET,          403, 403, ],
    [ 42,  "@action=deny  @method=GET",                   "@action=deny  @method=GET",                   DENY_GET,           403, 200, ],
    [ 43,  "@action=deny  @method=GET",                   "@action=deny  @method=GET",                   DENY_GET_AND_POST,  403, 403, ],
    [ 44,  "@action=deny  @method=GET",                   "@action=allow @method=POST",                  ALLOW_GET_AND_POST, 403, 200, ],
    [ 45,  "@action=deny  @method=GET",                   "@action=allow @method=POST",                  ALLOW_GET,          403, 403, ],
    [ 46,  "@action=deny  @method=GET",                   "@action=allow @method=POST",                  DENY_GET,           403, 200, ],
    [ 47,  "@action=deny  @method=GET",                   "@action=allow @method=POST",                  DENY_GET_AND_POST,  403, 403, ],
    [ 48,  "@action=deny  @method=GET",                   "@action=deny  @method=POST",                  ALLOW_GET_AND_POST, 403, 403, ],
    [ 49,  "@action=deny  @method=GET",                   "@action=deny  @method=POST",                  ALLOW_GET,          403, 403, ],
    [ 50,  "@action=deny  @method=GET",                   "@action=deny  @method=POST",                  DENY_GET,           403, 403, ],
    [ 51,  "@action=deny  @method=GET",                   "@action=deny  @method=POST",                  DENY_GET_AND_POST,  403, 403, ],
    [ 52,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "",                                            ALLOW_GET_AND_POST, 200, 200, ],
    [ 53,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "",                                            ALLOW_GET,          200, 403, ],
    [ 54,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "",                                            DENY_GET,           403, 200, ],
    [ 55,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "",                                            DENY_GET_AND_POST,  403, 403, ],
    [ 56,  "@action=deny  @src_ip=127.0.0.1 @src_ip=::1", "",                                            ALLOW_GET_AND_POST, 403, 403, ],
    [ 57,  "@action=deny  @src_ip=127.0.0.1 @src_ip=::1", "",                                            ALLOW_GET,          403, 403, ],
    [ 58,  "@action=deny  @src_ip=127.0.0.1 @src_ip=::1", "",                                            DENY_GET,           403, 403, ],
    [ 59,  "@action=deny  @src_ip=127.0.0.1 @src_ip=::1", "",                                            DENY_GET_AND_POST,  403, 403, ],
    [ 60,  "@action=allow @src_ip=192.0.2.1/24",          "",                                            ALLOW_GET_AND_POST, 403, 403, ],
    [ 61,  "@action=allow @src_ip=192.0.2.1/24",          "",                                            ALLOW_GET,          403, 403, ],
    [ 62,  "@action=allow @src_ip=192.0.2.0/24",          "",                                            DENY_GET,           403, 403, ],
    [ 63,  "@action=allow @src_ip=192.0.2.0/24",          "",                                            DENY_GET_AND_POST,  403, 403, ],
    [ 64,  "@action=deny  @src_ip=192.0.2.1/24",          "",                                            ALLOW_GET_AND_POST, 200, 200, ],
    [ 65,  "@action=deny  @src_ip=192.0.2.1/24",          "",                                            ALLOW_GET,          200, 403, ],
    [ 66,  "@action=deny  @src_ip=192.0.2.0/24",          "",                                            DENY_GET,           403, 200, ],
    [ 67,  "@action=deny  @src_ip=192.0.2.0/24",          "",                                            DENY_GET_AND_POST,  403, 403, ],
    [ 68,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "@action=deny @method=POST",                   ALLOW_GET_AND_POST, 200, 403, ],
    [ 69,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "@action=deny @method=POST",                   ALLOW_GET,          200, 403, ],
    [ 70,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "@action=deny @method=POST",                   DENY_GET,           403, 403, ],
    [ 71,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "@action=deny @method=POST",                   DENY_GET_AND_POST,  403, 403, ],
    [ 72,  "@action=deny  @method=POST",                  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", ALLOW_GET_AND_POST, 200, 403, ],
    [ 73,  "@action=deny  @method=POST",                  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", ALLOW_GET,          200, 403, ],
    [ 74,  "@action=deny  @method=POST",                  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", DENY_GET,           403, 403, ],
    [ 75,  "@action=deny  @method=POST",                  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", DENY_GET_AND_POST,  403, 403, ],
]
# yapf: enable

all_acl_combination_tests = [dict(zip(keys, test)) for test in all_acl_combinations]
