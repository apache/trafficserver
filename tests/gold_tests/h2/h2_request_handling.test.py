'''
Verify HTTP/2 client request handling through the fast-path handoff to HttpSM.
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
Verify HTTP/2 header handling through the fast-path handoff to HttpSM: request
URL normalization (explicit-port and IPv6 :authority), query-string caching,
and response emission (bodyless 204/304/HEAD and response-header preservation).
'''

Test.ATSReplayTest(replay_file="replay/h2_request_handling.replay.yaml")
