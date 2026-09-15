'''
Verify that ATS forwards origin HTTP/2 responses for HEAD requests and
304 responses to conditional GETs without spurious "Bad payload length"
stream errors when both client and origin use HTTP/2.
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

Test.Summary = '''
Verify outbound HTTP/2 payload-length validation honors RFC 9110 8.6 for
HEAD responses and for 304 responses to conditional GETs.
'''

Test.ContinueOnFail = True

replay_file = "replay_h2o_payload_validation/payload_validation.replay.yaml"

server = Test.MakeVerifierServerProcess("h2-payload-origin", replay_file)

ts = Test.MakeATSProcess("ts", enable_tls=True)
ts.addDefaultSSLFiles()
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http2',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 4,
        # Negotiate HTTP/2 to the origin via ALPN.
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        # Disable caching so the conditional GET reaches the origin.
        'proxy.config.http.cache.http': 0,
    })

ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.https_port}')
ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr = Test.AddTestRun("HEAD/304 with Content-Length over outbound HTTP/2")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client-payload", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.TimeOut = 60

# These warnings would indicate a regression of the outbound payload
# preclusion logic. They must not appear when ATS correctly recognizes the
# original HEAD or conditional GET request method.
ts.Disk.diags_log.Content = Testers.ExcludesExpression(
    "Bad payload length", "ATS must not report a bad payload length for HEAD/304 outbound H2 responses")
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    "recv data bad payload length", "ATS must not raise a stream PROTOCOL_ERROR for HEAD/304 outbound H2 responses")
