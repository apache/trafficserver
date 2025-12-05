'''
Verify filter_body plugin for request/response body content filtering.
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

Test.Summary = 'Verify filter_body plugin for request/response body content filtering.'

Test.SkipUnless(Condition.PluginExists('filter_body.so'))

# Test 1: Log only mode - request passes through, pattern logged
Test.ATSReplayTest(replay_file="replay/log_only.replay.yaml")

# Test 2: Add header action - request passes, header added
Test.ATSReplayTest(replay_file="replay/add_header.replay.yaml")

# Test 3: Header mismatch - no body inspection, request passes
Test.ATSReplayTest(replay_file="replay/header_mismatch.replay.yaml")

# Note: Block mode closes connection rather than returning 403
# This is validated manually - pattern detection and blocking works
# but generating a clean HTTP error response from a request transform
# requires additional infrastructure (like TSHttpTxnServerIntercept)
