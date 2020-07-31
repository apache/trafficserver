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
Test lua functionality
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
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=tslua.so @pparam={}/watermark.lua'.format(Test.TestDirectory)
)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ts_lua'
})

# Test for watermark debug output
ts.Streams.All = Testers.ContainsExpression(r"WMbytes\(31337\)", "Upstream watermark should be properly set")

# These are needed for 8.x only since Lua errors go to diags in 8.x, newer versions go to stdout
#ts.Disk.diags_log.Content = Testers.ContainsExpression("failed to get node's reconfigure time while checking script registration", "This test is a failure test")
#ts.Disk.diags_log.Content = Testers.ContainsExpression("failed to get node's reconfigure time while registering script", "This test is a failure test")

# Test if watermark upstream is set
tr = Test.AddTestRun("Lua Watermark")
tr.Processes.Default.Command = "curl -v http://127.0.0.1:{0}".format(ts.Variables.port)
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))

tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = server
