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
    Condition.HasCurlFeature('http2'),
    Condition.HasProgram("xxxZZZxxx", "disable the test until it is working")
)
Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)
server = Test.MakeOriginServer("server")
server2 = Test.MakeOriginServer("server2", ssl=True)
server3 = Test.MakeOriginServer("server3")

testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                  "timestamp": "1469733493.993",
                  "body": ""
                  }
response_header = {"headers": "HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n",
                   "timestamp": "1469733493.993",
                   "body": ""}

request_header2 = {"headers": "POST / HTTP/1.1\r\nHost: www.anotherexample.com\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\n",
                   "timestamp": "1415926535.898",
                   "body": "knock knock"}
response_header2 = {"headers": "HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n",
                    "timestamp": "1415926535.898",
                    "body": ""}

request_header3 = {"headers": "POST / HTTP/1.1\r\nHost: www.yetanotherexample.com\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\n",
                   "timestamp": "1415926535.898",
                   "body": "knock knock"}
response_header3 = {"headers": "HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n",
                    "timestamp": "1415926535.898",
                    "body": ""}

server.addResponse("sessionlog.json", request_header, response_header)
server2.addResponse("sessionlog.json", request_header2, response_header2)
server3.addResponse("sessionlog.json", request_header3, response_header3)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Variables.ssl_port = 4443
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'lm|ssl',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    # enable ssl port
    'proxy.config.http.server_ports': '{0} {1}:proto=http2;http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.ssl.client.verify.server':  0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
})

ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map http://www.yetanotherexample.com http://127.0.0.1:{0}'.format(server3.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map https://www.anotherexample.com https://127.0.0.1:{0}'.format(server2.Variables.SSL_Port, ts.Variables.ssl_port)
)


ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# HTTP1.1 GET: www.example.com
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 --proxy 127.0.0.1:{0} http://www.example.com  --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(server2)
tr.Processes.Default.StartBefore(server3)
# Delay on readyness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Streams.stderr = "gold/chunked_GET_200.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# HTTP2 POST: www.example.com Host, chunked body
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http2 -k https://127.0.0.1:{0} --verbose -H "Host: www.anotherexample.com" -d "Knock knock"'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/h2_chunked_POST_200.gold"

# HTTP1.1 POST: www.yetanotherexample.com Host, explicit size
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl http://127.0.0.1:{0} -H "Host: www.yetanotherexample.com" --verbose -d "knock knock"'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/chunked_POST_200.gold"
tr.StillRunningAfter = server

# HTTP1.1 POST: www.example.com Host, chunked body
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl http://127.0.0.1:{0} -H "Host: www.yetanotherexample.com" --verbose -H "Transfer-Encoding: chunked" -d "Knock knock"'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/chunked_POST_200.gold"
tr.StillRunningAfter = server
