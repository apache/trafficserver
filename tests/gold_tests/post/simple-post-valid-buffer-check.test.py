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

Test.Summary = '''
Make sure we have a valid buffer to write on. This used to make ats crash.
'''
Test.ContinueOnFail = True

# Use microserver instead of httpbin
server = Test.MakeOriginServer("server")

# Add a simple response for POST
request_header = {"headers": "POST /post HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "OK"
}
server.addResponse("sessionfile.log", request_header, response_header)

ts = Test.MakeATSProcess("ts")

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.request_buffer_enabled': 1,
        'proxy.config.http.number_of_redirections': 1,
    })

test_run = Test.AddTestRun("post buffer test")
test_run.Processes.Default.StartBefore(server)
test_run.Processes.Default.StartBefore(ts)
test_run.Processes.Default.Command = f'curl -v -H "Expect: 100-continue" -d "abc" http://127.0.0.1:{ts.Variables.port}/post'
test_run.Processes.Default.ReturnCode = 0
test_run.StillRunningAfter = server
test_run.StillRunningAfter = ts  # TS should not crash

# Verify request with Expect header was processed
ts.Disk.traffic_out.Content += Testers.ContainsExpression("100-continue", "Has Expect header")
# Verify ATS handled the POST request (no crash)
ts.Disk.traffic_out.Content += Testers.ContainsExpression("client post", "POST tunnel started")
