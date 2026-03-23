'''
Test heuristic caching of status codes per RFC 9110 Section 15.1.
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
Test heuristic caching of status codes per RFC 9110 Section 15.1.

Verifies that responses with heuristically cacheable status codes (200, 203,
204, 300, 301, 308, 410) are cached when only Last-Modified is present, and
that non-cacheable codes (302, 307, 400, 403) are not.
'''

Test.ATSReplayTest(replay_file="replay/cache-heuristic-status.replay.yaml")
