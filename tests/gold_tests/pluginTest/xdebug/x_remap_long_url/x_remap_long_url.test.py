"""
Verify xdebug X-Remap header injection does not OOB read with long URLs.
"""
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
Send a request with a URL long enough that the combined from/to URL
output exceeds the 2KB stack buffer xdebug uses for the X-Remap header.
Previously, snprintf's would-have-written return value was passed
straight to TSMimeHdrFieldValueStringInsert, causing an OOB read of
adjacent stack memory into the response header.
'''

Test.SkipUnless(Condition.PluginExists('xdebug.so'))

Test.ATSReplayTest(replay_file='x_remap_long_url.replay.yaml')
