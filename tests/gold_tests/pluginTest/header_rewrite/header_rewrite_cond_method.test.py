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
Test header_rewrite with METHOD conditions and operators.
'''

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = "header_rewrite_method_condition"
request_get = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_delete = {"headers": "DELETE / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
session_file = "sessionfile.log"
server.addResponse(session_file, request_get, response)
server.addResponse(session_file, request_delete, response)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'header.*',
        'proxy.config.http.insert_response_via_str': 0,
    })
# The following rule inserts a via header if the request method is a GET or DELETE
conf_name = "rule_cond_method.conf"
ts.Setup.CopyAs('rules/{0}'.format(conf_name), Test.RunDirectory)
ts.Disk.plugin_config.AddLine('header_rewrite.so {0}/{1}'.format(Test.RunDirectory, conf_name))
ts.Disk.remap_config.AddLine('map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port))

# Test method in READ_REQUEST_HDR_HOOK.
expected_output = "gold/header_rewrite_cond_method.gold"
expected_log = "gold/header_rewrite-tag.gold"
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --proxy 127.0.0.1:{0} "http://www.example.com" -H "Proxy-Connection: keep-alive" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = expected_output
tr.StillRunningAfter = server
ts.Disk.traffic_out.Content = expected_log

# Test method in SEND_REQUEST_HDR_HOOK.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --request DELETE --proxy 127.0.0.1:{0} "http://www.example.com" -H "Proxy-Connection: keep-alive" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = expected_output
tr.StillRunningAfter = server
ts.Disk.traffic_out.Content = expected_log
