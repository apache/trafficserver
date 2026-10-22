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

# yapf: disable
keys = ["index", "named_acl_1", "named_acl_2", "ip_allow", "GET response", "POST response"]
named_filter_combinations = [
    [  0,  "@action=deny  @method=POST",                  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", ALLOW_GET_AND_POST, 200, 403, ],
    [  1,  "@action=deny  @method=POST",                  "@action=allow @src_ip=192.0.2.1/24",          ALLOW_GET_AND_POST, 403, 403, ],
    [  2,  "@action=allow @src_ip=127.0.0.1 @src_ip=::1", "@action=deny  @method=POST",                  ALLOW_GET_AND_POST, 200, 403, ],
    [  3,  "@action=allow @src_ip=192.0.2.1/24",          "@action=deny  @method=POST",                  ALLOW_GET_AND_POST, 403, 403, ],
    [  4,  "@action=allow @method=POST",                  "@action=deny  @src_ip=127.0.0.1 @src_ip=::1", ALLOW_GET_AND_POST, 403, 403, ],
    [  5,  "@action=allow @method=POST",                  "@action=deny  @src_ip=192.0.2.1/24",          ALLOW_GET_AND_POST, 403, 200, ],
    [  6,  "@action=deny  @src_ip=127.0.0.1 @src_ip=::1", "@action=allow @method=POST",                  ALLOW_GET_AND_POST, 403, 403, ],
    [  7,  "@action=deny  @src_ip=192.0.2.1/24",          "@action=allow @method=POST",                  ALLOW_GET_AND_POST, 403, 200, ],
]
# yapf: enable

named_filter_combination_tests = [dict(zip(keys, test)) for test in named_filter_combinations]
