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

Test.Summary = '''
Test different combinations of TLS handshake hooks to ensure they are applied consistently.
'''

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server", ssl=True)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl_verify_test',
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.client.verify.server.policy': 'ENFORCED',
        'proxy.config.ssl.client.verify.server.properties': 'NONE',
        'proxy.config.url_remap.pristine_host_hdr': 1
    })

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.remap_config.AddLine(
    'map https://foo.com:{1}/ https://127.0.0.1:{0}'.format(server.Variables.SSL_Port, ts.Variables.ssl_port))
ts.Disk.remap_config.AddLine(
    'map https://bar.com:{1}/ https://127.0.0.1:{0}'.format(server.Variables.SSL_Port, ts.Variables.ssl_port))
ts.Disk.remap_config.AddLine(
    'map https://random.com:{1}/ https://127.0.0.1:{0}'.format(server.Variables.SSL_Port, ts.Variables.ssl_port))

ts.Disk.sni_yaml.AddLine('sni:')
ts.Disk.sni_yaml.AddLine('- fqdn: bar.com')
ts.Disk.sni_yaml.AddLine('  verify_server_policy: PERMISSIVE')

Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_verify_test.so'), ts, '-count=2 -bad=random.com -bad=bar.com')

tr = Test.AddTestRun("request good name")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --resolve \"foo.com:{0}:127.0.0.1\" -k  https://foo.com:{0}".format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have failed")

tr2 = Test.AddTestRun("request bad name")
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server
tr2.Processes.Default.Command = "curl --resolve \"random.com:{0}:127.0.0.1\" -k  https://random.com:{0}".format(
    ts.Variables.ssl_port)
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Curl attempt should have failed")

tr3 = Test.AddTestRun("request bad name permissive")
tr3.StillRunningAfter = ts
tr3.StillRunningAfter = server
tr3.Processes.Default.Command = "curl --resolve \"bar.com:{0}:127.0.0.1\" -k  https://bar.com:{0}".format(ts.Variables.ssl_port)
tr3.Processes.Default.ReturnCode = 0
tr3.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have failed")

# Overriding the built in ERROR check since we expect tr2 to fail
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "WARNING: TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin certificate check for 127.0.0.1.  Action=Terminate SNI=random.com",
    "random.com should fail")
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "WARNING: TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin certificate check for 127.0.0.1.  Action=Continue SNI=bar.com",
    "bar.com should fail but continue")
ts.Disk.diags_log.Content += Testers.ExcludesExpression("SNI=foo.com", "foo.com should not fail in any way")

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"Server verify callback 0 [\da-fx]+? - event is good SNI=foo.com good HS", "verify callback happens 2 times")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"Server verify callback 1 [\da-fx]+? - event is good SNI=foo.com good HS", "verify callback happens 2 times")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"Server verify callback 0 [\da-fx]+? - event is good SNI=random.com error HS", "verify callback happens 2 times")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"Server verify callback 1 [\da-fx]+? - event is good SNI=random.com error HS", "verify callback happens 2 times")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"Server verify callback 0 [\da-fx]+? - event is good SNI=bar.com error HS", "verify callback happens 2 times")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"Server verify callback 1 [\da-fx]+? - event is good SNI=bar.com error HS", "verify callback happens 2 times")
ts.Disk.traffic_out.Content += Testers.ContainsExpression("Server verify callback SNI APIs match=true", "verify SNI names match")
