'''
Test a PERMISSIVE SERVER_VERIFY_HOOK failure with a TLS 1.2 origin
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
A TS_SSL_VERIFY_SERVER_HOOK failure under a PERMISSIVE policy must let a TLS 1.2
origin handshake finish, without retries.
'''

ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server", ssl=True)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
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
        # With TLS 1.2 the verify hook runs while the handshake is still in
        # progress, and without the session cache every retry runs it again.
        'proxy.config.ssl.client.TLSv1_3.enabled': 0,
        'proxy.config.ssl.origin_session_cache.enabled': 0,
        'proxy.config.url_remap.pristine_host_hdr': 1
    })

ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

ts.Disk.remap_config.AddLine(
    'map https://bar.com:{1}/ https://127.0.0.1:{0}'.format(server.Variables.SSL_Port, ts.Variables.ssl_port))

ts.Disk.sni_yaml.AddLine('sni:')
ts.Disk.sni_yaml.AddLine('- fqdn: bar.com')
ts.Disk.sni_yaml.AddLine('  verify_server_policy: PERMISSIVE')

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_verify_test.so'), ts, '-count=1 -bad=bar.com')

tr = Test.AddTestRun("request bad name permissive over TLS 1.2")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.MakeCurlCommand(
    "--resolve \"bar.com:{0}:127.0.0.1\" -k -s -o /dev/null -w 'status=%{{http_code}}\\n' https://bar.com:{0}".format(
        ts.Variables.ssl_port),
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("status=200", "The permissive origin connection should succeed")

ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "WARNING: TS_EVENT_SSL_VERIFY_SERVER plugin failed the origin certificate check for 127.0.0.1.  Action=Continue SNI=bar.com",
    "bar.com should fail but continue")

# One origin handshake, not one per connect retry.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    r"Server verify callback 0 [\da-fx]+? - event is good SNI=bar.com error HS", "The verify callback ran")
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r"(?s)Server verify callback 0 [^\n]*SNI=bar.com.*Server verify callback 0 [^\n]*SNI=bar.com",
    "The origin handshake should not be retried")
