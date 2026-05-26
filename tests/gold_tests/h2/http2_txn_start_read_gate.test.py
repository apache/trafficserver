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
Verify HTTP/2 read events do not bypass async TXN_START hooks.
'''

Test.SkipUnless(Condition.HasProxyVerifierVersion('2.8.0'))

replay_file = "replay/http2_txn_start_read_gate.replay.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)

ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)
ts.addDefaultSSLFiles()
ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{server.Variables.http_port}")
ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'delay_txn_start',
        'proxy.config.ssl.server.cert.path': f"{ts.Variables.SSLDir}",
        'proxy.config.ssl.server.private_key.path': f"{ts.Variables.SSLDir}",
    })

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'delay_txn_start.so'), ts, '500')

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(r'\[ERROR\]', 'Proxy Verifier should not report errors.')
tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'Equals Success: Key: "1", Content Data: "body", Value: "response-body"', 'Client should receive the response body.')

server.Streams.All += Testers.ContainsExpression(
    'Equals Success: Key: "1", Content Data: "body", Value: "request-body"', 'Origin should receive the request body.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "delayed TXN_START reenable", "The test plugin should delay and then resume TXN_START.")
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    "READ_REQUEST_HDR before delayed TXN_START reenable", "READ_REQUEST_HDR must wait for TXN_START reenable.")
