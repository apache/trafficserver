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
Test for a regression of the issue fixed in
https://github.com/apache/trafficserver/pull/5423
Insertion of header rewrite directives in to the output
'''

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'header.*',
})

# The following rule adds X-First and X-Last headers
ts.Setup.CopyAs('rules/rule_l_value.conf', Test.RunDirectory)

ts.Disk.plugin_config.AddLine(
    'header_rewrite.so {0}/rule_l_value.conf'.format(Test.RunDirectory)
)
ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map http://www.example.com:8080 http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# [L] test
tr = Test.AddTestRun("Header Rewrite End [L]")
tr.Processes.Default.Command = 'curl --proxy 127.0.0.1:{0} "http://www.example.com" -H "Proxy-Connection: keep-alive" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = "gold/header_rewrite-l_value.gold"
tr.StillRunningAfter = server
ts.Streams.All = "gold/header_rewrite-tag.gold"
