'''
Bundle of cripts feature tests, driven by one replay-file harness. Each
.cript under files/ targets a distinct cripts feature; the replay file
configures one remap rule per cript and a transaction set per feature.

Currently bundled:
  - bundle_headers.cript : exercises cripts::Bundle::Headers across all
                           four hooks with every dynamic substitution
                           source (ID/IP/CIDR/CLIENT-URL components),
                           via the HRWBridge / %{...} syntax.
  - direct_headers.cript : same data sources but via the direct cripts
                           API (UUID/IP/Url accessors), bypassing
                           Bundle::Headers entirely.
  - server_bundle_headers.cript : Bundle::Headers on the two server hooks
                           — rm_headers (incl. Authorization/Cookie/
                           Set-Cookie) plus set_headers with literal
                           values (add + overwrite). Server-only so it
                           also pins the regression where rm_headers()
                           misrouted both server targets into the
                           client-response list and silently dropped the
                           strips.

Concurrent client sessions widen the exposure surface; under a TSAN
build this is the regression target for any bug that puts shared mutable
state back into a Cripts plugin instance.
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
cripts: bundle of feature tests (bundle_headers.cript + direct_headers.cript)
'''

Test.SkipUnless(Condition.HasATSFeature('TS_HAS_CRIPTS'))

Test.ATSReplayTest(replay_file="cripts_bundle.replay.yaml")
