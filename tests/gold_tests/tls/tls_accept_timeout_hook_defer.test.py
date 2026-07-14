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
Verify that a TLS accept/handshake inactivity timer that fires while a
TS_HTTP_SSN_START_HOOK callout is still pending is handled cleanly.

When the SSN_START hook defers its reenable, the read VIO can be reassigned to
a session acceptor before the session takes ownership. A pre-session
inactivity timer that fires in that window must be handled without an
unexpected assertion, and the client must still get a normal response.

Setup: set the TLS handshake and accept-no-activity timeouts to 1 second, load
a plugin that delays the SSN_START hook by 3 seconds, then drive TLS requests
over HTTP/1.1 and HTTP/2. The client should receive normal 200 responses.
'''

import os

Test.Summary = 'TLS accept timeout is handled cleanly when the SSN_START hook defers.'

replay_file = "tls_accept_timeout_hook_defer.replay.yaml"

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

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.http_port}/')

# Delay TS_HTTP_SSN_START_HOOK reenable by 3s — longer than both
# handshake_timeout_in and accept_no_activity_timeout (1s each), shorter than
# the verifier's 5s read timeout. Either timer firing during the hook window
# lands a VC_EVENT_INACTIVITY_TIMEOUT on the session acceptor's VIO cont.
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'hook_add_plugin.so'), ts, '-delay-ms=3000')

# traffic_server must handle the stale inactivity timer during the SSN_START
# hook window without an unexpected assertion.
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r'failed assertion `event == NET_EVENT_ACCEPT', 'session acceptors must handle stale inactivity timeouts')
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    r'FATAL.*Assertion', 'traffic_server must not assert on a stale accept-path inactivity timeout')

tr = Test.AddTestRun("TLS request through delayed SSN_START hook")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client", replay_file, https_ports=[ts.Variables.ssl_port])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
