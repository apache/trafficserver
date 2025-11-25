'''
Verify HTTP body buffering with request_buffer.so plugin.
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

Test.Summary = 'Verify HTTP body buffering with request_buffer.so plugin.'

Test.SkipUnless(Condition.PluginExists('request_buffer.so'))

Test.ATSReplayTest(replay_file="replay/body_buffer.replay.yaml")

# Test for issue #6900: post_copy_size=0 with request_buffer_enabled should not cause 403.
Test.ATSReplayTest(replay_file="replay/zero_post_copy_size.replay.yaml")
