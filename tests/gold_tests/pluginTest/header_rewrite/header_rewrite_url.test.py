'''
Test header_rewrite with URL conditions and operators.
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
Test header_rewrite with URL conditions and operators.
'''

Test.ContinueOnFail = True
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

# Configure the server to return 200 responses. The rewrite rules below set a
# non-200 status, so if curl gets a 200 response something went wrong.
Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: no_path.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'header.*',
})
# The following rule changes the status code returned from origin server to 303
ts.Setup.CopyAs('rules/rule_client.conf', Test.RunDirectory)
ts.Setup.CopyAs('rules/set_redirect.conf', Test.RunDirectory)

# This configuration makes use of CLIENT-URL in conditions.
ts.Disk.remap_config.AddLine(
    'map http://www.example.com/from_path/ https://127.0.0.1:{0}/to_path/ '
    '@plugin=header_rewrite.so @pparam={1}/rule_client.conf'.format(server.Variables.Port, Test.RunDirectory))
ts.Disk.remap_config.AddLine(
    'map http://www.example.com:8080/from_path/ https://127.0.0.1:{0}/to_path/ '
    '@plugin=header_rewrite.so @pparam={1}/rule_client.conf'.format(server.Variables.Port, Test.RunDirectory))
# This configuration makes use of TO-URL in a set-redirect operator.
ts.Disk.remap_config.AddLine(
    'map http://no_path.com http://no_path.com?name=brian/ '
    '@plugin=header_rewrite.so @pparam={0}/set_redirect.conf'.format(Test.RunDirectory))

# Test CLIENT-URL.
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --proxy 127.0.0.1:{0} "http://www.example.com/from_path/hello?=foo=bar" '
    '-H "Proxy-Connection: keep-alive" --verbose'.format(ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = "gold/header_rewrite-client.gold"
tr.StillRunningAfter = server
ts.Disk.traffic_out.Content = "gold/header_rewrite-tag.gold"

# Test TO-URL in a set-redirect operator.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --head 127.0.0.1:{0} -H "Host: no_path.com" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/set-redirect.gold"
tr.StillRunningAfter = server
ts.Disk.traffic_out.Content = "gold/header_rewrite-tag.gold"
