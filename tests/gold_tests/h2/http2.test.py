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

import os
Test.Summary = '''
Test a basic remap of a http connection
'''
# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work"),
    Condition.HasCurlFeature('http2')
)
Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)
server = Test.MakeOriginServer("server")

requestLocation = "test2"
reHost = "www.example.com"

testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
request_header2 = {
    "headers": "GET /{0} HTTP/1.1\r\nHost: {1}\r\n\r\n".format(requestLocation, reHost), "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header2 = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n",
                    "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# Add info for the large H2 download test
server.addResponse("sessionlog.json",
                   {"headers": "GET /bigfile HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""},
                   {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nCache-Control: max-age=3600\r\nContent-Length: 191414\r\n\r\n", "timestamp": "1469733493.993", "body": ""})

post_body = "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
server.addResponse("sessionlog.jason",
                   {"headers": "POST /postchunked HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                    "timestamp": "1469733493.993",
                     "body": post_body},
                   {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nContent-Length: 10\r\n\r\n", "timestamp": "1469733493.993", "body": "0123456789"})

server.addResponse("sessionlog.json", request_header2, response_header2)
# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Variables.ssl_port = 4443
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http2',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    # enable ssl port
    'proxy.config.http.server_ports': '{0} {1}:proto=http2;http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.http.cache.http': 0,
    'proxy.config.ssl.client.verify.server':  0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.http2.active_timeout_in': 3,
    'proxy.config.http2.max_concurrent_streams_in': 65535,
})
ts.Setup.CopyAs('h2client.py', Test.RunDirectory)
ts.Setup.CopyAs('h2bigclient.py', Test.RunDirectory)
ts.Setup.CopyAs('h2chunked.py', Test.RunDirectory)
ts.Setup.CopyAs('h2active_timeout.py', Test.RunDirectory)

# Test Case 1:  basic H2 interaction
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python3 h2client.py -p {0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Streams.stdout = "gold/remap-200.gold"
tr.StillRunningAfter = server

# Test Case 2: Make sure all the big file gets back.  Regression test for issue 1646
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python3 h2bigclient.py -p {0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/bigfile.gold"
tr.StillRunningAfter = server

# Test Case 3: Chunked content
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python3 h2chunked.py -p {0}  -u /{1}'.format(ts.Variables.ssl_port, requestLocation)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/chunked.gold"
tr.StillRunningAfter = server

# NOTE: Skipping this test run because traffic-replay doesn't currently support H2
# Test Case 4: Multiple request
# client_path = os.path.join(Test.Variables.AtsTestToolsDir, 'traffic-replay/')
# tr = Test.AddTestRun()
# tr.Processes.Default.Command = "python3 {0} -type {1} -log_dir {2} -port {3} -host '127.0.0.1' -s_port {4} -v -colorize False".format(
#     client_path, 'h2', server.Variables.DataDir, ts.Variables.port, ts.Variables.ssl_port)
# tr.Processes.Default.ReturnCode = 0
# tr.Processes.Default.Streams.stdout = "gold/replay.gold"
# tr.StillRunningAfter = server

# Test Case 5:h2_active_timeout
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python3 h2active_timeout.py -p {0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = "gold/active_timeout.gold"
tr.StillRunningAfter = server

# Test Case 6: Post with chunked body
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -k -H "Transfer-Encoding: chunked" -d "{0}" https://127.0.0.1:{1}/postchunked'.format( post_body, ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = "gold/post_chunked.gold"
tr.StillRunningAfter = server
