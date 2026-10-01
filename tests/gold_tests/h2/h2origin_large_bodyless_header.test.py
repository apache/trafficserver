'''Verify bodyless HTTP/2 origin responses with header blocks over 4 KB.'''
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

Test.Summary = __doc__

# An HTTP/2 origin response that ends the stream on its HEADERS frame is handed
# to the HttpSM as one buffer spanning several IOBuffer blocks, with EOS set.
# Every response here must reach the client with the origin's status and
# headers rather than a 5xx.
Test.ATSReplayTest(replay_file="replay/h2origin_large_bodyless_header.replay.yaml")
