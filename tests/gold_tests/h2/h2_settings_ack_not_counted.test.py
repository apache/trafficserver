'''Verify solicited origin SETTINGS ACKs do not consume the receive limit.'''
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
Verify SETTINGS-ACK frames are not counted against
`proxy.config.http2.max_settings_frames_per_minute`.
'''

Test.ContinueOnFail = True

replay_file = "replay_h2_settings_ack/settings_ack.replay.yaml"

server = Test.MakeVerifierServerProcess("settings-ack-origin", replay_file)

ts = Test.MakeATSProcess("ts", enable_tls=True)
ts.addDefaultSSLFiles()
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http2_cs',
        # Reuse an origin connection with dynamic outbound windows. Each
        # new stream elicits an ACK while the origin sends only its initial
        # non-ACK SETTINGS frame.
        'proxy.config.http2.max_settings_frames_per_minute': 2,
        'proxy.config.http2.flow_control.policy_out': 2,
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.http.server_session_sharing.match': 'ip',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 1,
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

tr = Test.AddTestRun("Drive 5 H/2 client sessions, each with one request")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client-settings-ack", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.TimeOut = 60

# Once the client run finishes, assert that ATS never tripped the
# SETTINGS-frames-per-minute limit and never recorded a connection-level
# error caused by it. `stdout_wait` retries until the gold matches, so it
# tolerates the brief settle time between the last response and the
# metric updates.
tr = Test.AddTestRun("Assert SETTINGS-ACK frames did not trip the per-minute limit")
tr.Processes.Default.Command = (
    f"{Test.Variables.AtsTestToolsDir}/stdout_wait"
    f" 'traffic_ctl metric get"
    f" proxy.process.http2.max_settings_frames_per_minute_exceeded"
    f" proxy.process.http2.connection_errors'"
    f" {Test.TestDirectory}/gold/h2-settings-ack-metrics.gold")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

ts.Disk.diags_log.Content = Testers.ExcludesExpression(
    "too frequent SETTINGS frames", "must not reject solicited origin SETTINGS ACKs")
