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
Verify proxy.config.regex.anchor_policy_rules at the ControlMatcher call site.
'''

# cache.config, parent.config and splitdns.config share one RegexMatcher::NewEntry(),
# so driving cache.config url_regex pins the label and the anchoring for all three.
# Core only, so this does not depend on which plugins a build enables.

# Default (0): the rule still applies and is named with its directive, file and line.
Test.ATSReplayTest(replay_file="replay/regex_anchor_cache_config_off.replay.yaml")

# Enabled (1): the unanchored rule can no longer match, so its action never fires.
Test.ATSReplayTest(replay_file="replay/regex_anchor_cache_config_on.replay.yaml")
