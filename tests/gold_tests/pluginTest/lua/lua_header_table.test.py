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
Test lua header table functionality
'''

Test.SkipUnless(
    Condition.PluginExists('tslua.so'),
)

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nSet-Cookie: test1\r\nSet-Cookie: test2\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=tslua.so @pparam={}/header_table.lua'.format(Test.TestDirectory)
)

# Test - Check for header table
tr = Test.AddTestRun("Lua Header Table ")
ps = tr.Processes.Default  # alias
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = "curl -v http://127.0.0.1:{0}".format(ts.Variables.port)
ps.Env = ts.Env
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression("test1test2", "expected header table results")
tr.StillRunningAfter = ts
