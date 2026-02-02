'''
Test cache_open_write_fail_action = 5 (READ_RETRY mode)
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
Test cache_open_write_fail_action = 5 (READ_RETRY mode) to verify:
1. Basic read-while-writer behavior with fail_action=5
2. READ_RETRY mode configuration is accepted and functional
3. System does not crash under write lock contention
4. Requests are served correctly when read retries are exhausted
'''

Test.ContinueOnFail = True

Test.ATSReplayTest(replay_file="replay/cache-read-retry-basic.replay.yaml")
Test.ATSReplayTest(replay_file="replay/cache-read-retry-exhausted.replay.yaml")
