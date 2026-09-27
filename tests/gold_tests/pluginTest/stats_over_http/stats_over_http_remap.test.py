'''
Verify stats_over_http as a remap plugin.
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

Test.Summary = 'stats_over_http serves the stats from a remap rule'
Test.SkipUnless(Condition.PluginExists('stats_over_http.so'))
Test.ATSReplayTest(replay_file='replay/remap.replay.yaml')
Test.ATSReplayTest(replay_file='replay/remap_acl_deny.replay.yaml')
