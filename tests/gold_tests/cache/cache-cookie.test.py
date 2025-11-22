'''
Test cookie-related caching behaviors
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
Test cookie-related caching behaviors
'''

# Verify correct caching behavior in default configuration
Test.ATSReplayTest(replay_file="replay/cookie-default.replay.yaml")

# Verify correct caching behavior when not caching responses to cookies
Test.ATSReplayTest(replay_file="replay/cookie-bypass-cache.replay.yaml")

# Verify correct caching behavior when caching only image responses to cookies
Test.ATSReplayTest(replay_file="replay/cookie-cache-img-only.replay.yaml")

# Verify correct caching behavior when caching all but text responses to cookies
Test.ATSReplayTest(replay_file="replay/cookie-all-but-text.replay.yaml")

# Verify correct caching behavior for all but text with exceptions
Test.ATSReplayTest(replay_file="replay/cookie-all-but-text-with-excp.replay.yaml")
