'''
Test whitespace between field name and colon in the header
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
Test whitespace between field name and colon in the header
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

# **testname is required**
testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.remap_config.AddLine('map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port))

# Test 0 - 200 Response
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H " foo: bar" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "syntax.200.gold"
tr.StillRunningAfter = ts

# Test 1 - 400 Response - Single space after field name
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H "foo : bar" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "syntax.400.gold"
tr.StillRunningAfter = ts

# Test 2 - 400 Response - Double space after field name
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H "foo  : bar" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "syntax.400.gold"
tr.StillRunningAfter = ts

# Test 3 - 400 Response - Three different Content-Length headers
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -d "hello world" -H "Content-Length: 11" -H "Content-Length: 10" -H "Content-Length: 9" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "syntax.400.gold"
tr.StillRunningAfter = ts

# Test 4 - 200 Response - Three same Content-Length headers
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -d "hello world" -H "Content-Length: 11" -H "Content-Length: 11" -H "Content-Length: 11" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "syntax.200.gold"
tr.StillRunningAfter = ts

# Test 5 - 200 Response - Three different Content-Length headers with a Transfer encoding header
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -d "hello world" -H "Transfer-Encoding: chunked" -H "Content-Length: 11" -H "Content-Length: 10" -H "Content-Length: 9" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "syntax.200.gold"
tr.StillRunningAfter = ts
