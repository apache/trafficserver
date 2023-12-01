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
Test http/2 GET method that has a body
'''

Test.SkipUnless(Condition.HasProxyVerifierVersion('2.8.0'))

pv_server = Test.MakeVerifierServerProcess("pv_server", "h2get_with_body.yaml")

ts = Test.MakeATSProcess('ts', select_ports=True, enable_tls=True, enable_cache=True)

ts.addDefaultSSLFiles()
ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")
ts.Disk.records_config.update(
    {
        "proxy.config.http.server_ports": f"{ts.Variables.port} {ts.Variables.ssl_port}:ssl",
        "proxy.config.http.background_fill_active_timeout": "0",
        "proxy.config.http.background_fill_completed_threshold": "0.0",
        "proxy.config.http.cache.required_headers": 0,  # Force cache
        "proxy.config.http.insert_response_via_str": 2,
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 1,
        'proxy.config.ssl.server.cert.path': f"{ts.Variables.SSLDir}",
        'proxy.config.ssl.server.private_key.path': f"{ts.Variables.SSLDir}",
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        "proxy.config.diags.debug.enabled": 3,
        "proxy.config.diags.debug.tags": "http",
    })

ts.Disk.remap_config.AddLines([f'map / https://127.0.0.1:{pv_server.Variables.https_port}'])

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(pv_server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess(
    "pv_client",
    "h2get_with_body.yaml",
    http_ports=[ts.Variables.port],
    https_ports=[ts.Variables.ssl_port],
    other_args='--thread-limit 1')
tr.Processes.Default.ReturnCode = 0

tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'Equals Success: Key: "1", Content Data: "body", Value: "server_test"', 'Response check')

pv_server.Streams.All += Testers.ContainsExpression(
    'Equals Success: Key: "1", Content Data: "body", Value: "client_test"', 'Request check')
