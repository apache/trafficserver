"""
Verify parent_retry does not mark the parent down for non-retryable requests.
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
A POST that elicits a configured unavailable_server_retry response code (502)
from the parent must not cause the parent to be marked down.
'''

Test.ATSReplayTest(replay_file='replays/parent_retry_non_retryable_post.replay.yaml')
