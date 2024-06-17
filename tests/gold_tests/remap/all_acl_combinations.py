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

# From src/proxy/http/remap/RemapConfig.cc:123
# // ACLs are processed in this order:
# // 1. A remap.config ACL line for an individual remap rule.
# // 2. All named ACLs in remap.config.
# // 3. Rules as specified in ip_allow.yaml.
#
# If proxy.config.url_remap.acl_matching_policy is 0 (default), ATS employs "the first explicit match wins" policy. This means
# if it's implict match, ATS continue to process ACL filters. OTOH, if it's 1, ATS only process the first ACL filter.
#
# A simple example is:
#
# +--------+------------------+------------------+------------------+------------------+---------------+
# | Method |   remap.config   |    named ACL 1   |    named ACL 2   |  ip_allow.yaml   |    result     |
# +--------+------------------+------------------+------------------+------------------+---------------+
# | GET    | deny  (explicit) | -                | -                | -                | denied  (403) |
# | HEAD   | allow (implicit) | deny  (explicit) | -                | -                | denied  (403) |
# | POST   | allow (implicit) | allow (implicit) | allow (explicit) | -                | allowed (200) |
# | PUT    | allow (implicit) | allow (implicit) | deny  (implicit) | deny  (explicit) | deny    (403) |
# | DELETE | allow (implicit) | allow (implicit) | deny  (implicit) | allow (implicit) | allowed (200) |
# |--------+------------------+------------------+------------------+------------------+---------------+
'''
replay_proxy_response writes the given replay file (which expects a single GET & POST client-request)
with the given proxy_response value. This is only used to support the below table test.
'''

# yapf: disable
keys = ["index", "policy", "inline", "named_acl", "ip_allow", "GET response", "POST response"]
all_acl_combinations = [
    [  0,  "explicit",  "",                          "",                           ALLOW_GET_AND_POST, 200, 200, ],
    [  1,  "explicit",  "",                          "",                           ALLOW_GET,          200, 403, ],
    [  2,  "explicit",  "",                          "",                           DENY_GET,           403, 200, ],
    [  3,  "explicit",  "",                          "",                           DENY_GET_AND_POST,  403, 403, ],
    [  4,  "explicit",  "",                          "@action=allow @method=GET",  ALLOW_GET_AND_POST, 200, 200, ],
    [  5,  "explicit",  "",                          "@action=allow @method=GET",  ALLOW_GET,          200, 403, ],
    [  6,  "explicit",  "",                          "@action=allow @method=GET",  DENY_GET,           200, 200, ],
    [  7,  "explicit",  "",                          "@action=allow @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [  8,  "explicit",  "",                          "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [  9,  "explicit",  "",                          "@action=deny  @method=GET",  ALLOW_GET,          403, 403, ],
    [ 10,  "explicit",  "",                          "@action=deny  @method=GET",  DENY_GET,           403, 200, ],
    [ 11,  "explicit",  "",                          "@action=deny  @method=GET",  DENY_GET_AND_POST,  403, 403, ],
    [ 12,  "explicit",  "@action=allow @method=GET", "",                           ALLOW_GET_AND_POST, 200, 200, ],
    [ 13,  "explicit",  "@action=allow @method=GET", "",                           ALLOW_GET,          200, 403, ],
    [ 14,  "explicit",  "@action=allow @method=GET", "",                           DENY_GET,           200, 200, ],
    [ 15,  "explicit",  "@action=allow @method=GET", "",                           DENY_GET_AND_POST,  200, 403, ],
    [ 16,  "explicit",  "@action=allow @method=GET", "@action=allow @method=GET",  ALLOW_GET_AND_POST, 200, 200, ],
    [ 17,  "explicit",  "@action=allow @method=GET", "@action=allow @method=GET",  ALLOW_GET,          200, 403, ],
    [ 18,  "explicit",  "@action=allow @method=GET", "@action=allow @method=GET",  DENY_GET,           200, 200, ],
    [ 19,  "explicit",  "@action=allow @method=GET", "@action=allow @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 20,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 200, 200, ],
    [ 21,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=GET",  ALLOW_GET,          200, 403, ],
    [ 22,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=GET",  DENY_GET,           200, 200, ],
    [ 23,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 24,  "explicit",  "@action=allow @method=GET", "@action=allow @method=POST", ALLOW_GET_AND_POST, 200, 200, ],
    [ 25,  "explicit",  "@action=allow @method=GET", "@action=allow @method=POST", ALLOW_GET,          200, 200, ],
    [ 26,  "explicit",  "@action=allow @method=GET", "@action=allow @method=POST", DENY_GET,           200, 200, ],
    [ 27,  "explicit",  "@action=allow @method=GET", "@action=allow @method=POST", DENY_GET_AND_POST,  200, 200, ],
    [ 28,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=POST", ALLOW_GET_AND_POST, 200, 403, ],
    [ 29,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=POST", ALLOW_GET,          200, 403, ],
    [ 30,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=POST", DENY_GET,           200, 403, ],
    [ 31,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=POST", DENY_GET_AND_POST,  200, 403, ],
    [ 32,  "explicit",  "@action=deny  @method=GET", "",                           ALLOW_GET_AND_POST, 403, 200, ],
    [ 33,  "explicit",  "@action=deny  @method=GET", "",                           ALLOW_GET,          403, 403, ],
    [ 34,  "explicit",  "@action=deny  @method=GET", "",                           DENY_GET,           403, 200, ],
    [ 35,  "explicit",  "@action=deny  @method=GET", "",                           DENY_GET_AND_POST,  403, 403, ],
    [ 36,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 37,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=GET",  ALLOW_GET,          403, 403, ],
    [ 38,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=GET",  DENY_GET,           403, 200, ],
    [ 39,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=GET",  DENY_GET_AND_POST,  403, 403, ],
    [ 40,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 41,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=GET",  ALLOW_GET,          403, 403, ],
    [ 42,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=GET",  DENY_GET,           403, 200, ],
    [ 43,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=GET",  DENY_GET_AND_POST,  403, 403, ],
    [ 44,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=POST", ALLOW_GET_AND_POST, 403, 200, ],
    [ 45,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=POST", ALLOW_GET,          403, 200, ],
    [ 46,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=POST", DENY_GET,           403, 200, ],
    [ 47,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=POST", DENY_GET_AND_POST,  403, 200, ],
    [ 48,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=POST", ALLOW_GET_AND_POST, 403, 403, ],
    [ 49,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=POST", ALLOW_GET,          403, 403, ],
    [ 50,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=POST", DENY_GET,           403, 403, ],
    [ 51,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=POST", DENY_GET_AND_POST,  403, 403, ],
    [ 52,  "any",       "",                          "",                           ALLOW_GET_AND_POST, 200, 200, ],
    [ 53,  "any",       "",                          "",                           ALLOW_GET,          200, 403, ],
    [ 54,  "any",       "",                          "",                           DENY_GET,           403, 200, ],
    [ 55,  "any",       "",                          "",                           DENY_GET_AND_POST,  403, 403, ],
    [ 56,  "any",       "",                          "@action=allow @method=GET",  ALLOW_GET_AND_POST, 200, 403, ],
    [ 57,  "any",       "",                          "@action=allow @method=GET",  ALLOW_GET,          200, 403, ],
    [ 58,  "any",       "",                          "@action=allow @method=GET",  DENY_GET,           200, 403, ],
    [ 59,  "any",       "",                          "@action=allow @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 60,  "any",       "",                          "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 61,  "any",       "",                          "@action=deny  @method=GET",  ALLOW_GET,          403, 200, ],
    [ 62,  "any",       "",                          "@action=deny  @method=GET",  DENY_GET,           403, 200, ],
    [ 63,  "any",       "",                          "@action=deny  @method=GET",  DENY_GET_AND_POST,  403, 200, ],
    [ 64,  "any",       "@action=allow @method=GET", "",                           ALLOW_GET_AND_POST, 200, 403, ],
    [ 65,  "any",       "@action=allow @method=GET", "",                           ALLOW_GET,          200, 403, ],
    [ 66,  "any",       "@action=allow @method=GET", "",                           DENY_GET,           200, 403, ],
    [ 67,  "any",       "@action=allow @method=GET", "",                           DENY_GET_AND_POST,  200, 403, ],
    [ 68,  "any",       "@action=allow @method=GET", "@action=allow @method=GET",  ALLOW_GET_AND_POST, 200, 403, ],
    [ 69,  "any",       "@action=allow @method=GET", "@action=allow @method=GET",  ALLOW_GET,          200, 403, ],
    [ 70,  "any",       "@action=allow @method=GET", "@action=allow @method=GET",  DENY_GET,           200, 403, ],
    [ 71,  "any",       "@action=allow @method=GET", "@action=allow @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 72,  "any",       "@action=allow @method=GET", "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 200, 403, ],
    [ 73,  "any",       "@action=allow @method=GET", "@action=deny  @method=GET",  ALLOW_GET,          200, 403, ],
    [ 74,  "any",       "@action=allow @method=GET", "@action=deny  @method=GET",  DENY_GET,           200, 403, ],
    [ 75,  "any",       "@action=allow @method=GET", "@action=deny  @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 76,  "any",       "@action=deny  @method=GET", "",                           ALLOW_GET_AND_POST, 403, 200, ],
    [ 77,  "any",       "@action=deny  @method=GET", "",                           ALLOW_GET,          403, 200, ],
    [ 78,  "any",       "@action=deny  @method=GET", "",                           DENY_GET,           403, 200, ],
    [ 79,  "any",       "@action=deny  @method=GET", "",                           DENY_GET_AND_POST,  403, 200, ],
    [ 80,  "any",       "@action=deny  @method=GET", "@action=allow @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 81,  "any",       "@action=deny  @method=GET", "@action=allow @method=GET",  ALLOW_GET,          403, 200, ],
    [ 82,  "any",       "@action=deny  @method=GET", "@action=allow @method=GET",  DENY_GET,           403, 200, ],
    [ 83,  "any",       "@action=deny  @method=GET", "@action=allow @method=GET",  DENY_GET_AND_POST,  403, 200, ],
    [ 84,  "any",       "@action=deny  @method=GET", "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 85,  "any",       "@action=deny  @method=GET", "@action=deny  @method=GET",  ALLOW_GET,          403, 200, ],
    [ 86,  "any",       "@action=deny  @method=GET", "@action=deny  @method=GET",  DENY_GET,           403, 200, ],
    [ 87,  "any",       "@action=deny  @method=GET", "@action=deny  @method=GET",  DENY_GET_AND_POST,  403, 200, ],
]
# yapf: enable

all_acl_combination_tests = [dict(zip(keys, test)) for test in all_acl_combinations]
