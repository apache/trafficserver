'''
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

import os

Test.Summary = '''
Make sure we have a valid buffer to write on. This used to make ats crash.
'''
Test.ContinueOnFail = True

server = Test.MakeHttpBinServer("server")
ts = Test.MakeATSProcess("ts")

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts.Disk.records_config.update(
    '''
diags:
    debug:
        enabled: 1
        tags: http
http:
    request_buffer_enabled: 1
    number_of_redirections: 1

''')

test_run = Test.AddTestRun("post buffer test")
test_run.Processes.Default.StartBefore(server)
test_run.Processes.Default.StartBefore(ts)
test_run.MakeCurlCommand(f' -v -H "Expect: 100-continue" -d "abc" http://localhost:{ts.Variables.port}/post', ts=ts)
ts.Disk.traffic_out.Content += Testers.ContainsExpression("HTTP/1.1 100 Continue", "Has Expect header")
ts.Disk.traffic_out.Content += Testers.ContainsExpression("HTTP/1.1 200 OK", "200OK")
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts  # TS should not crash
test_run.Processes.Default.ReturnCode = 0
