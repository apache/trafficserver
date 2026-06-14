'''
Verify regex_map performs full-hostname matching.

A regex_map rule must match the entire request hostname, never a host in
which the configured value appears only as a substring. For a rule covering
"cdn.example.com":
  - "prefix.cdn.example.com"   must NOT match: rejected by the start anchor.
  - "cdn.example.com.evil.com" must NOT match: rejected by full-hostname matching
                               (start-anchoring alone would still match it).
  - "cdn.example.computer"     must NOT match: shared prefix, no label boundary.
  - "cdn.example.com."         must NOT match: trailing-dot FQDN.
  - "cdn.example.com"          must match (exact), and reach the origin.
  - "CDN.EXAMPLE.COM"          must match (case-insensitive), and reach the origin.
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

Test.Summary = '''
Verify regex_map performs full-hostname matching.
'''

Test.ATSReplayTest(replay_file="replay/regex_map_anchor.replay.yaml")
