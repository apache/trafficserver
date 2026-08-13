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
Verify proxy.config.regex.anchor_policy_rules in both states.
'''

# Default (0): matching is unchanged and unanchored rules are reported.
Test.ATSReplayTest(replay_file="replay/regex_anchor_policy_off.replay.yaml")

# Enabled (1): a gate pattern must match the whole subject.
Test.ATSReplayTest(replay_file="replay/regex_anchor_policy_on.replay.yaml")
