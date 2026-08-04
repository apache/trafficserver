'''
Verify HTTP/2 outbound (SM -> origin) server-request handling: the request
header is handed to the outbound stream without a serialize+reparse.
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
Verify HTTP/2 outbound server-request handling: the server request header is
handed to the outbound Http2Stream without a serialize+reparse, and reaches an
HTTP/2 origin byte-correct (GET with query, POST with body).
'''

Test.ATSReplayTest(replay_file="replay/h2_outbound_request_handling.replay.yaml")
