'''
Test SERVER_VERIFY_HOOK
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

Test.SkipUnless(Condition.HasOpenSSLVersion("1.1.1"))

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server", ssl=True)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Disk.records_config.update({
    # Test looks for debug output from the plugin
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl_client_verify_test',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.CA.cert.filename': '{0}/signer.pem'.format(ts.Variables.SSLDir),
    'proxy.config.url_remap.pristine_host_hdr': 1
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map https://foo.com:{1}/ https://127.0.0.1:{0}'.format(server.Variables.SSL_Port, ts.Variables.ssl_port)
)
ts.Disk.remap_config.AddLine(
    'map https://bar.com:{1}/ https://127.0.0.1:{0}'.format(server.Variables.SSL_Port, ts.Variables.ssl_port)
)
ts.Disk.remap_config.AddLine(
    'map https://random.com:{1}/ https://127.0.0.1:{0}'.format(server.Variables.SSL_Port, ts.Variables.ssl_port)
)

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: bar.com',
    '  verify_client: STRICT',
    '- fqdn: foo.com',
    '  verify_client: STRICT',
])

Test.PreparePlugin(os.path.join(Test.Variables.AtsTestToolsDir, 'plugins', 'ssl_client_verify_test.cc'), ts, '-count=2 -good=foo.com')

tr = Test.AddTestRun("request good name")
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-foo.key")
tr.Setup.Copy("ssl/signed-bar.pem")
tr.Setup.Copy("ssl/signed-bar.key")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}/case1".format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.all = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")


tr2 = Test.AddTestRun("request bad name")
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server
tr2.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./signed-bar.pem --key ./signed-bar.key --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}/case1".format(ts.Variables.ssl_port)
tr2.Processes.Default.ReturnCode = 35
tr2.Processes.Default.Streams.all = Testers.ContainsExpression("error", "Curl attempt should have failed")

tr3 = Test.AddTestRun("request badly signed cert")
tr3.Setup.Copy("ssl/server.pem")
tr3.Setup.Copy("ssl/server.key")
tr3.StillRunningAfter = ts
tr3.StillRunningAfter = server
tr3.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./server.pem --key ./server.key --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}/case1".format(ts.Variables.ssl_port)
tr3.Processes.Default.ReturnCode = 35
tr3.Processes.Default.Streams.all = Testers.ContainsExpression("error", "Curl attempt should have failed")

ts.Streams.All += Testers.ContainsExpression("Client verify callback 0 [\da-fx]+? - event is good good HS", "verify callback happens 2 times")
ts.Streams.All += Testers.ContainsExpression("Client verify callback 1 [\da-fx]+? - event is good good HS", "verify callback happens 2 times")
ts.Streams.All += Testers.ContainsExpression("Client verify callback 0 [\da-fx]+? - event is good error HS", "verify callback happens 2 times")
ts.Streams.All += Testers.ContainsExpression("Client verify callback 1 [\da-fx]+? - event is good error HS", "verify callback happens 2 times")
