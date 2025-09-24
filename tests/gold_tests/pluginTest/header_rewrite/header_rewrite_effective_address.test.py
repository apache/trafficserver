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
Test header_rewrite set-effective-address operator.
'''

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = "header_rewrite_effective_address"
request_get = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
session_file = "sessionfile.log"
server.addResponse(session_file, request_get, response)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'header_rewrite|dbg_header_rewrite',
    })
# The following rule inserts a via header if the request method is a GET or DELETE
conf_name = "rule_effective_address.conf"
ts.Setup.CopyAs(f'rules/{conf_name}', Test.RunDirectory)
ts.Disk.plugin_config.AddLine(f'header_rewrite.so {Test.RunDirectory}/{conf_name}')
ts.Disk.remap_config.AddLine(f'map http://www.example.com http://127.0.0.1:{server.Variables.Port}')

# Test that the IP address in Real-IP request header is returned in Effective-IP response header.
expected_output = "gold/header_rewrite_effective_address.gold"
tr = Test.AddTestRun()
tr.MakeCurlCommand(
    f'--http1.1 -H "Host: www.example.com" -H "Real-IP: 1.2.3.4" --verbose "http://127.0.0.1:{ts.Variables.port}"', ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = expected_output
tr.StillRunningAfter = server
