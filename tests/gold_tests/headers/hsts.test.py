'''
Test the hsts response header.
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

import os
Test.Summary = '''
heck hsts header is set correctly
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)
server = Test.MakeOriginServer("server")

#**testname is required**
testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# ATS Configuration
ts.addSSLfile("../remap/ssl/server.pem")
ts.addSSLfile("../remap/ssl/server.key")

ts.Variables.ssl_port = 4443
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.ssl.hsts_max_age': 300,
})

ts.Disk.remap_config.AddLine(
    'map https://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Test 1 - 200 Response
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Command = (
    'curl -s -D - --verbose --ipv4 --http1.1 --insecure --header "Host: {0}" https://localhost:{1}'
    .format('www.example.com', ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "hsts.200.gold"
tr.StillRunningAfter = ts

# Test 2 - 404 Not Found on Accelerator
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl -s -D - --verbose --ipv4 --http1.1 --insecure --header "Host: {0}" https://localhost:{1}'
    .format('bad_host', ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "hsts.404.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
