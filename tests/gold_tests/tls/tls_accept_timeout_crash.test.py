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
'''
Regression test: stale TLS accept-timeout events must not crash the session acceptors.

With a pending TS_HTTP_SSN_START_HOOK whose callout defers, a stale TLS
handshake or accept-no-activity inactivity timer could fire after the read VIO
had been reassigned to a session acceptor but before the session took
ownership, triggering a release_assert in the acceptor's mainEvent and aborting
traffic_server.

Reproduction: set the TLS handshake and accept-no-activity timeouts to 1
second, load a plugin that delays the SSN_START hook by 3 seconds, then drive
TLS requests over HTTP/1.1 and HTTP/2. Before the fix, traffic_server aborts
during the delay window. After the fix, the client receives normal 200
responses.
'''

import os

Test.Summary = 'TLS accept timeout must not crash HttpSessionAccept when SSN_START hook defers.'

replay_file = "tls_accept_timeout_crash.replay.yaml"

server = Test.MakeVerifierServerProcess("server", replay_file)

ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)
ts.addDefaultSSLFiles()

ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.handshake_timeout_in': 1,
        'proxy.config.http.accept_no_activity_timeout': 1,
        'proxy.config.http2.accept_no_activity_timeout': 1,
        'proxy.config.url_remap.remap_required': 0,
    })

ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.http_port}/')

# Delay TS_HTTP_SSN_START_HOOK reenable by 3s — longer than both
# handshake_timeout_in and accept_no_activity_timeout (1s each), shorter than
# the verifier's 5s read timeout. Either timer firing during the hook window
# lands a VC_EVENT_INACTIVITY_TIMEOUT on the session acceptor's VIO cont.
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'hook_add_plugin.so'), ts, '-delay-ms=3000')

# Assert traffic_server never hits the release_assert landmine in the session
# acceptors when an inactivity timer fires during the SSN_START hook window.
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r'failed assertion `event == NET_EVENT_ACCEPT', 'session acceptors must not abort on stale inactivity timeouts')
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    r'FATAL.*Assertion', 'traffic_server must not abort on a stale accept-path inactivity timeout')

tr = Test.AddTestRun("TLS request through delayed SSN_START hook")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client", replay_file, https_ports=[ts.Variables.ssl_port])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
