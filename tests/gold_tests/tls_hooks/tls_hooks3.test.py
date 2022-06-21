'''
Test single delayed cert callback
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
Test different combinations of TLS handshake hooks to ensure they are applied consistently.
'''

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addDefaultSSLFiles()

ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1,
                               'proxy.config.diags.debug.tags': 'ssl_hook_test',
                               'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
                               'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
                               })

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map https://example.com:{0} http://127.0.0.1:{1}'.format(ts.Variables.ssl_port, server.Variables.Port)
)

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_hook_test.so'), ts, '-cert=1')

tr = Test.AddTestRun("Test one cert hook")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'curl -k -H \'host:example.com:{0}\' https://127.0.0.1:{0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/preaccept-1.gold"

ts.Disk.traffic_out.Content = "gold/ts-cert-1.gold"

certstring = "Cert callback 0"
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    "\A(?:(?!{0}).)*{0}(?!.*{0}).*\Z".format(certstring), "Cert message appears only once", reflags=re.S | re.M)

tr.Processes.Default.TimeOut = 15
tr.TimeOut = 15
