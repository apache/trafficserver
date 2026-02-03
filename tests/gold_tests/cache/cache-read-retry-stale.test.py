'''
Test cache_open_write_fail_action = 6 (READ_RETRY with stale fallback)
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
Smoke test for cache_open_write_fail_action = 6 (READ_RETRY_STALE_ON_REVALIDATE) to verify:
1. Action 6 is accepted by the configuration system
2. Basic caching works correctly with action 6 enabled
3. The system does not crash

Note: The stale fallback behavior (serving stale on read retry exhaustion) is
difficult to reliably test due to timing sensitivity. This test focuses on
verifying the feature is functional without causing instability.
'''

Test.ContinueOnFail = True

Test.ATSReplayTest(replay_file="replay/cache-read-retry-stale.replay.yaml")
