'''
Verify compress appends Accept-Encoding to an existing origin Vary header.
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
Verify compress appends Accept-Encoding to an origin Vary header whose value is
a prefix of Accept-Encoding.
'''

Test.SkipUnless(Condition.PluginExists('compress.so'))

Test.ATSReplayTest(replay_file="replay/compress-vary-accept.replay.yaml")
