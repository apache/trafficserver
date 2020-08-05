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

Test.Summary = 'Testing ATS inactivity timeout'

Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server", delay=8)

request_header = {"headers": "GET /file HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "5678", "body": ""}

server.addResponse("sessionfile.log", request_header, response_header)

ts.addSSLfile("../tls/ssl/server.pem")
ts.addSSLfile("../tls/ssl/server.key")

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.url_remap.remap_required': 1,
    'proxy.config.http.transaction_no_activity_timeout_out': 2,
})

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}/'.format(server.Variables.Port))

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

tr = Test.AddTestRun("tr")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts, ready=When.PortOpen(ts.Variables.port))
tr.Processes.Default.Command = 'curl -i  http://127.0.0.1:{0}/file'.format(ts.Variables.port)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "Inactivity Timeout", "Request should fail with inactivity timeout")

tr2 = Test.AddTestRun("tr")
tr2.Processes.Default.Command = 'curl -k -i --http1.1 https://127.0.0.1:{0}/file'.format(ts.Variables.ssl_port)
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "Inactivity Timeout", "Request should fail with inactivity timeout")

tr3 = Test.AddTestRun("tr")
tr3.Processes.Default.Command = 'curl -k -i --http2 https://127.0.0.1:{0}/file'.format(ts.Variables.ssl_port)
tr3.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "Inactivity Timeout", "Request should fail with inactivity timeout")
