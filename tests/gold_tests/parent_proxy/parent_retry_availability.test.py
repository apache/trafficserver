"""
Verify the retry path restores a parent only when the retry actually succeeds.
"""
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
Both outcomes of a parent retry: a parent that is still up but not serving stays
marked down, and a parent that has recovered is restored to the pool.
'''

Test.ATSReplayTest(replay_file='replays/parent_retry_failure_stays_down.replay.yaml')
Test.ATSReplayTest(replay_file='replays/parent_retry_success_restores.replay.yaml')
