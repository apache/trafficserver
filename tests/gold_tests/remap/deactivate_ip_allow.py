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
keys = ["index", "policy", "inline", "named_acl", "deactivate_ip_allow", "ip_allow", "GET response", "POST response"]
deactivate_ip_allow_combinations = [
    [  0,  "explicit",  "",                          "", False, ALLOW_GET_AND_POST, 200, 200,   ],
    [  1,  "explicit",  "",                          "", False, ALLOW_GET,          200, 403,   ],
    [  2,  "explicit",  "",                          "", False, DENY_GET,           403, 200,   ],
    [  3,  "explicit",  "",                          "", False, DENY_GET_AND_POST,  403, 403,   ],
    [  4,  "explicit",  "",                          "", False, DENY_ALL,           None, None, ],
    [  5,  "explicit",  "",                          "", True,  ALLOW_GET_AND_POST, 200, 200,   ],
    [  6,  "explicit",  "",                          "", True,  ALLOW_GET,          200, 200,   ],
    [  7,  "explicit",  "",                          "", True,  DENY_GET,           200, 200,   ],
    [  8,  "explicit",  "",                          "", True,  DENY_GET_AND_POST,  200, 200,   ],
    [  9,  "explicit",  "",                          "", True,  DENY_ALL,           200, 200,   ],
    [ 10,  "explicit",  "@action=allow @method=GET", "", False, ALLOW_GET_AND_POST, 200, 200,   ],
    [ 11,  "explicit",  "@action=allow @method=GET", "", False, ALLOW_GET,          200, 403,   ],
    [ 12,  "explicit",  "@action=allow @method=GET", "", False, DENY_GET,           200, 200,   ],
    [ 13,  "explicit",  "@action=allow @method=GET", "", False, DENY_GET_AND_POST,  200, 403,   ],
    [ 14,  "explicit",  "@action=allow @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 15,  "explicit",  "@action=allow @method=GET", "", True,  ALLOW_GET_AND_POST, 200, 200,   ],
    [ 16,  "explicit",  "@action=allow @method=GET", "", True,  ALLOW_GET,          200, 200,   ],
    [ 17,  "explicit",  "@action=allow @method=GET", "", True,  DENY_GET,           200, 200,   ],
    [ 18,  "explicit",  "@action=allow @method=GET", "", True,  DENY_GET_AND_POST,  200, 200,   ],
    [ 19,  "explicit",  "@action=allow @method=GET", "", True,  DENY_ALL,           200, 200,   ],
    [ 20,  "explicit",  "@action=deny  @method=GET", "", False, ALLOW_GET_AND_POST, 403, 200,   ],
    [ 21,  "explicit",  "@action=deny  @method=GET", "", False, ALLOW_GET,          403, 403,   ],
    [ 22,  "explicit",  "@action=deny  @method=GET", "", False, DENY_GET,           403, 200,   ],
    [ 23,  "explicit",  "@action=deny  @method=GET", "", False, DENY_GET_AND_POST,  403, 403,   ],
    [ 24,  "explicit",  "@action=deny  @method=GET", "", False, DENY_ALL,           None, None, ],
    [ 25,  "explicit",  "@action=deny  @method=GET", "", True,  ALLOW_GET_AND_POST, 403, 200,   ],
    [ 26,  "explicit",  "@action=deny  @method=GET", "", True,  ALLOW_GET,          403, 200,   ],
    [ 27,  "explicit",  "@action=deny  @method=GET", "", True,  DENY_GET,           403, 200,   ],
    [ 28,  "explicit",  "@action=deny  @method=GET", "", True,  DENY_GET_AND_POST,  403, 200,   ],
    [ 29,  "explicit",  "@action=deny  @method=GET", "", True,  DENY_ALL,           403, 200,   ],
]
all_deactivate_ip_allow_tests = [dict(zip(keys, test)) for test in deactivate_ip_allow_combinations]
# yapf: enable
