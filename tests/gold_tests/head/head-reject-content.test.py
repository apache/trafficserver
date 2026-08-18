'''
Verify HEAD requests carrying declared body content are rejected.
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
Verify HEAD requests with a non-zero Content-Length or chunked
Transfer-Encoding are rejected with 400, HEAD requests without declared
content are forwarded normally, proxy.config.http.reject_head_with_content
set to 0 forwards a HEAD request body to the origin, and a HEAD request
carrying a body via HTTP/2 DATA frames (with no Content-Length header) is
also rejected.
'''

Test.SkipUnless(Condition.HasProxyVerifierVersion('2.8.0'))

Test.ATSReplayTest(replay_file='replay/head-reject-content.replay.yaml')
