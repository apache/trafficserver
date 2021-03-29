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

Test.Summary = '''
Verify that the client-side keep alive is honored for TLS and different versions of HTTP
'''

Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.TLSv1_3': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.log.max_secs_per_buffer': 1
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map https://example.com:{0} http://127.0.0.1:{1}'.format(ts.Variables.ssl_port, server.Variables.Port)
)

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: testformat
      format: '%<cqssl> %<cqtr>'
  logs:
    - mode: ascii
      format: testformat
      filename: squid
'''.split("\n")
)

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_hook_test.so'), ts, '-preaccept=1')

tr = Test.AddTestRun("Test two HTTP/1.1 requests over one TLS connection")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -v --http1.1  -H \'host:example.com:{0}\' https://127.0.0.1:{0} https://127.0.0.1:{0}'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Test two HTTP/1.1 requests over two TLS connections")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -v --http1.1  -H \'host:example.com:{0}\' https://127.0.0.1:{0}; curl -k -v --http1.1 -H \'host:example.com:{0}\'  https://127.0.0.1:{0}'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Test two HTTP/2 requests over one TLS connection")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -v --http2  -H \'host:example.com:{0}\' https://127.0.0.1:{0} https://127.0.0.1:{0}'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Test two HTTP/2 requests over two TLS connections")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -v --http2  -H \'host:example.com:{0}\' https://127.0.0.1:{0}; curl -k -v --http1.1 -H \'host:example.com:{0}\'  https://127.0.0.1:{0}'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

# Just a check to flush out the traffic log until we have a clean shutdown for traffic_server
tr = Test.AddTestRun("Wait for the access log to write out")
tr.DelayStart = 5
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'ls'
tr.Processes.Default.ReturnCode = 0

ts.Disk.squid_log.Content = "gold/accesslog.gold"
