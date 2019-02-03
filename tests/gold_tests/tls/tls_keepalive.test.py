'''
Use pre-accept hook to verify that both requests are made over the same TLS session
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
import re

Test.Summary = '''
Verify that the client-side keep alive is honored for TLS and different versions of HTTP
'''

ts = Test.MakeATSProcess("ts", select_ports=False)
server = Test.MakeOriginServer("server")
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Variables.ssl_port = 4443
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    # enable ssl port
    'proxy.config.http.server_ports': '{0}:ssl'.format(ts.Variables.ssl_port),
    'proxy.config.ssl.TLSv1_3': 0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.log.max_secs_per_buffer': 1
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map https://example.com:4443 http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.logging_yaml.AddLines([
    'formats:',
    '- name: testformat',
    "  format: '%<cqssl> %<cqtr>'",
    "logs:",
    "- mode: ascii",
    "  format: testformat",
    "  filename: squid" ])

Test.PreparePlugin(os.path.join(Test.Variables.AtsTestToolsDir, 'plugins', 'ssl_hook_test.cc'), ts, '-preaccept=1')

tr = Test.AddTestRun("Test two HTTP/1.1 requests over one TLS connection")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -v --http1.1  -H \'host:example.com:{0}\' https://127.0.0.1:{0} https://127.0.0.1:{0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Test two HTTP/1.1 requests over two TLS connections")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -v --http1.1  -H \'host:example.com:{0}\' https://127.0.0.1:{0}; curl -k -v --http1.1 -H \'host:example.com:{0}\'  https://127.0.0.1:{0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Test two HTTP/2 requests over one TLS connection")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -v --http2  -H \'host:example.com:{0}\' https://127.0.0.1:{0} https://127.0.0.1:{0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Test two HTTP/2 requests over two TLS connections")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -v --http2  -H \'host:example.com:{0}\' https://127.0.0.1:{0}; curl -k -v --http1.1 -H \'host:example.com:{0}\'  https://127.0.0.1:{0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

# Just a check to flush out the traffic log until we have a clean shutdown for traffic_server
tr = Test.AddTestRun("Wait for the access log to write out")
tr.DelayStart = 5
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'ls'
tr.Processes.Default.ReturnCode = 0

ts.Disk.squid_log.Content = "gold/accesslog.gold"

