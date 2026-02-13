'''
Verify correct caching behavior with respect to request method.
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
Verify correct caching behavior with respect to request method.
'''

# Verify correct POST response handling when caching POST responses is disabled
Test.ATSReplayTest(replay_file="replay/post_with_post_caching_disabled.replay.yaml")

# Verify correct POST response handling when caching POST responses is enabled
Test.ATSReplayTest(replay_file="replay/post_with_post_caching_enabled.replay.yaml")

# Verify correct POST response handling when caching POST responses is enabled via overridable config
Test.ATSReplayTest(replay_file="replay/post_with_post_caching_override.replay.yaml")

# Verify correct HEAD response handling with cached GET response
Test.ATSReplayTest(replay_file="replay/head_with_get_cached.replay.yaml")

# Verify DELETE request handling - RFC 9111 4.4. Invalidating Stored Responses
Test.ATSReplayTest(replay_file="replay/delete_cached.replay.yaml")
