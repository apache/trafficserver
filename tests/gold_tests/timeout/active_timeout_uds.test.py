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

Test.Summary = 'Testing ATS active timeout'

Test.SkipUnless(Condition.HasCurlFeature('http2'))

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server", delay=8)

request_header = {"headers": "GET /file HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "5678", "body": ""}

server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.url_remap.remap_required': 1,
    'proxy.config.http.transaction_active_timeout_out': 2,
})

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}/'.format(server.Variables.Port))

tr = Test.AddTestRun("tr")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.MakeCurlCommand('-i  http://127.0.0.1:{0}/file'.format(ts.Variables.port))
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Activity Timeout", "Request should fail with active timeout")
