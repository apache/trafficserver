'''
Test cached responses and requests with bodies
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
Test cached responses and cache-control directives
'''

# Basic cache operations: miss, hit, no-cache-control, stale, and only-if-cached
Test.ATSReplayTest(replay_file="replay/cache-control-basic.replay.yaml")

# Max-age directives in both clients and responses
Test.ATSReplayTest(replay_file="replay/cache-control-max-age.replay.yaml")

# S-maxage directives in responses
Test.ATSReplayTest(replay_file="replay/cache-control-s-maxage.replay.yaml")

# Interaction between cache-control no-cache and pragma header
Test.ATSReplayTest(replay_file="replay/cache-control-pragma.replay.yaml")

# Request cache-control directives in default configuration
Test.ATSReplayTest(replay_file="replay/request-cache-control-default.replay.yaml")

# Request cache-control directives when ATS honors client requests to bypass cache
Test.ATSReplayTest(replay_file="replay/request-cache-control-honor-client.replay.yaml")

# Response cache-control directives in default configuration
Test.ATSReplayTest(replay_file="replay/response-cache-control-default.replay.yaml")

# Response cache-control directives when ATS ignores server requests to bypass cache
Test.ATSReplayTest(replay_file="replay/response-cache-control-ignored.replay.yaml")
