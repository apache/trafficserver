'''
Verify that exceeding MAX_SUBJECTS for proxy.config.acl.subjects logs an error and does not crash.
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
Verify that exceeding MAX_SUBJECTS for proxy.config.acl.subjects logs an error and does not crash.
'''

# Scenario 1: Configuring 4 ACL subjects (more than MAX_SUBJECTS=3) should log
# an error but not crash. Request should still succeed with 200 OK.
Test.ATSReplayTest(replay_file="replay/ip_allow_subjects_overflow.replay.yaml")

# Scenario 2: Configuring exactly 3 ACL subjects (equal to MAX_SUBJECTS) should
# work without error. Request should succeed with 200 OK.
Test.ATSReplayTest(replay_file="replay/ip_allow_subjects_valid.replay.yaml")
