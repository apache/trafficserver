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
Test tls
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")

# ssl-post is built via `make`. Here we copy the built binary down to the test
# directory so that the test runs in this file can use it.
Test.Setup.Copy(os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'tls', 'ssl-post'))

requestLocation = "test2"
reHost = "www.example.com"

testName = ""

header_count = 378
#header_count = 78

header_string = "POST /post HTTP/1.1\r\nHost: www.example.com\r\nContent-Length:1000\r\n"

for i in range(0, header_count):
    header_string = "{1}header{0}:{0}\r\n".format(i, header_string)
header_string = "{0}\r\n".format(header_string)

post_body = ""
for i in range(0, 1000):
    post_body = "{0}0".format(post_body)

# Add info the origin server responses
server.addResponse("sessionlog.json",
                   {"headers": header_string,
                    "timestamp": "1469733493.993",
                    "body": post_body},
                   {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nCache-Control: max-age=3600\r\nContent-Length: 2\r\n\r\n",
                       "timestamp": "1469733493.993",
                       "body": "ok"})

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts.Disk.records_config.update({'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir), 'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir), 'proxy.config.exec_thread.autoconfig.scale': 1.0, 'proxy.config.ssl.server.cipher_suite':
                               'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2', })

tr = Test.AddTestRun("Run-Test")
tr.Command = './ssl-post 127.0.0.1 40 {0} {1}'.format(header_count, ts.Variables.ssl_port)
tr.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Streams.stdout = "gold/ssl-post.gold"
tr.StillRunningAfter = server
