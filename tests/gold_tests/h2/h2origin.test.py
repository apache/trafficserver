'''
Test communication to origin with H2
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
Test communication to origin with H2
'''

Test.ContinueOnFail = True

#
# Communicate to origin with HTTP/2
#
ts = Test.MakeATSProcess("ts", enable_tls="true")

# add ssl materials like key, certificates for the server
ts.addDefaultSSLFiles()
replay_file = "replay/"
server = Test.MakeVerifierServerProcess("h2-origin", replay_file)
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.exec_thread.autoconfig': 0,
    # Allow for more parallelism
    'proxy.config.exec_thread.limit': 4,
    'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
    # Sticking with thread pool because global pool does not work with h2
    'proxy.config.http.server_session_sharing.pool': 'thread',
    'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
})

ts.Disk.remap_config.AddLine(
    'map / https://127.0.0.1:{0}'.format(server.Variables.https_port)
)
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: testformat
      format: '%<cqtq> %<ttms> %<crc> %<pssc> %<{uuid}cqh> %<cqpv> %<sqpv> %<cqssl> %<cqtr> %<pqssl> %<sstc>'
  logs:
    - mode: ascii
      format: testformat
      filename: squid
'''.split("\n")
)

tr = Test.AddTestRun("Test traffic to origin using HTTP/2")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])
tr.StillRunningAfter = ts
tr.TimeOut = 60

# Just a check to flush out the traffic log until we have a clean shutdown for traffic_server
tr = Test.AddTestRun("Wait for the access log to write out")
tr.DelayStart = 10
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'ls'
tr.Processes.Default.ReturnCode = 0

# UUIDs 1-4 should be http/1.1 clients and H2 origin
# UUIDs 5-9 should be http/2 clients and H2 origins
ts.Disk.squid_log.Content = Testers.ContainsExpression(" [1-4] http/1.1 http/2", "cases 1-4 request http/1.1")
ts.Disk.squid_log.Content += Testers.ExcludesExpression(" [1-4] http/2 http/2", "cases 1-4 request http/1.1")
ts.Disk.squid_log.Content = Testers.ContainsExpression(" 1[1-4] http/1.1 http/2", "cases 12-14 request http/1.1")
ts.Disk.squid_log.Content += Testers.ExcludesExpression(" 1[2-4] http/2 http/2", "cases 12-14 request http/1.1")
ts.Disk.squid_log.Content += Testers.ContainsExpression(" [5-9] http/2 http/2", "cases 5-11 request http/2")
ts.Disk.squid_log.Content += Testers.ExcludesExpression(" [5-9] http/1.1 http/2", "cases 5-11 request http/2")
ts.Disk.squid_log.Content += Testers.ContainsExpression(" 1[0-1] http/2 http/2", "cases 5-11 request http/2")
ts.Disk.squid_log.Content += Testers.ExcludesExpression(" 1[0-1] http/1.1 http/2", "cases 5-11 request http/2")
