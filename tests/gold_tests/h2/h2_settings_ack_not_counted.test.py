'''
Verify that SETTINGS frames carrying the ACK flag do not count against
`proxy.config.http2.max_settings_frames_per_minute`.

A SETTINGS-ACK is a mandatory protocol response to a SETTINGS frame ATS
itself sent (RFC 7540 / 9113 6.5), so it cannot be used by a peer to
flood ATS. Counting inbound ACKs against the per-minute receive limit
spuriously closed otherwise-healthy connections with ENHANCE_YOUR_CALM,
which is especially visible with
`proxy.config.http2.flow_control.policy_in=2`
(LARGE_SESSION_AND_DYNAMIC_STREAM): ATS sends a SETTINGS frame per
inbound stream and the client returns one ACK per stream.
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
        # Set the per-minute SETTINGS receive limit very low. The verifier
        # client will send 1 ACK in response to ATS's preface SETTINGS plus
        # 1 ACK per stream that ATS opens via dynamic-window SETTINGS. With
        # five separate inbound H/2 sessions, the buggy code would tear at
        # least one of them down with ENHANCE_YOUR_CALM after the second
        # inbound ACK. With the fix, ACKs are not counted and the limit is
        # never tripped.
        'proxy.config.http2.max_settings_frames_per_minute': 2,
        # `LARGE_SESSION_AND_DYNAMIC_STREAM` makes ATS send a SETTINGS
        # frame whenever a new inbound stream is created (to readjust the
        # per-stream window). Each of those SETTINGS frames triggers a
        # client ACK -- exactly the pattern that exposed the bug in
        # production.
        'proxy.config.http2.flow_control.policy_in': 2,
        'proxy.config.http.cache.http': 0,
    })

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.http_port}')
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
    "too frequent SETTINGS frames", "must not GOAWAY ENHANCE_YOUR_CALM on inbound SETTINGS-ACK frames")
