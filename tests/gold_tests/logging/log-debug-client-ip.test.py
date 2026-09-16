'''
Verify debug logging filtered by client IP.
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
Verify per-client-IP debug logging emits request and response header dumps.
'''

Test.ContinueOnFail = True

Test.ATSReplayTest(replay_file='replay/log-debug-client-ip-http.replay.yaml')
Test.ATSReplayTest(replay_file='replay/log-debug-client-ip-https.replay.yaml')
Test.ATSReplayTest(replay_file='replay/log-debug-client-ip-http2.replay.yaml')

if Condition.HasATSFeature('TS_HAS_QUICHE') and Condition.HasCurlFeature('http3'):
    Test.ATSReplayTest(replay_file='replay/log-debug-client-ip-http3.replay.yaml')
