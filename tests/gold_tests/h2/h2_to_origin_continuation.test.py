'''
Verify that ATS does not tear down outbound HTTP/2 connections, and does
not crash on a `_sm == nullptr` assertion, when the origin response
headers may be split across HEADERS + CONTINUATION frames on a stream
that has already advanced past IDLE.
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
import sys

from ports import get_port

Test.Summary = '''
Verify that ATS does not tear down outbound HTTP/2 connections and does
not crash on a `_sm == nullptr` assertion when receiving the origin's
response on a stream that is past the IDLE state.
'''

Test.ContinueOnFail = True

replay_file = "replay_h2o_continuation/continuation.replay.yaml"

tr = Test.AddTestRun("Outbound HTTP/2 CONTINUATION on a non-IDLE stream")
tr.Setup.Copy("continuation_origin.py")

server = tr.Processes.Process("h2-continuation-origin")
server_port = get_port(server, "https_port")
server_pem = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem")
server_key = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.key")
server.Setup.Copy(server_pem)
server.Setup.Copy(server_key)
server.Command = (f"{sys.executable} {tr.RunDirectory}/continuation_origin.py "
                  f"{server_port} server.pem server.key 2")
server.Ready = When.PortOpen(server_port)
server.ReturnCode = Any(0, -2)
server.Streams.stdout += Testers.ContainsExpression(
    r"sent_continuation_frames=[1-9][0-9]*",
    "The origin must positively verify that it emitted CONTINUATION frames.",
)

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
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.http.cache.http': 0,
        # The custom origin emits a header block larger than this value and
        # positively verifies that it generated CONTINUATION frames.
        'proxy.config.http2.max_frame_size': 16384,
        'proxy.config.http2.max_header_list_size': 1048576,
        'proxy.config.http.response_header_max_size': 65536,
    })

ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server_port}')
ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client-continuation", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])
tr.StillRunningAfter = ts
tr.TimeOut = 60

# Regression guards for the two outbound CONTINUATION bugs:
#   1) Connection-level PROTOCOL_ERROR ("continuation bad state") would
#      mean rcv_continuation_frame still rejects the OPEN /
#      HALF_CLOSED_LOCAL stream states on outbound connections.
#   2) The `_sm == nullptr` assertion (visible as a fatal in
#      traffic.out) would mean rcv_continuation_frame called
#      `new_transaction` on an outbound stream whose state machine
#      already exists (the SM was created when ATS issued the request).
ts.Disk.diags_log.Content = Testers.ExcludesExpression(
    "continuation bad state", "ATS must not raise a PROTOCOL_ERROR for outbound CONTINUATION frames in OPEN/HALF_CLOSED_LOCAL")
ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
    "_sm == nullptr", "ATS must not re-create an outbound HTTP/2 transaction on receipt of CONTINUATION")
