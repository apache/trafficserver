'''
Verify that ATS multiplexes multiple outbound HTTP/2 origin requests onto
a single H/2 connection when the configured server-session sharing pool is
`global`. This is a regression test for the latent bug where outbound H/2
sessions were filed exclusively in the per-thread pool while
`HttpSessionManager::acquire_session` was searching only the global pool,
silently disabling H/2 origin reuse and forcing a fresh TCP+TLS+H/2
handshake for every single request.
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
Verify outbound HTTP/2 origin connection reuse / multiplexing across
multiple requests when proxy.config.http.server_session_sharing.pool is
`global`.
'''

Test.ContinueOnFail = True

replay_file = "replay_h2o_pool_reuse/pool_reuse.replay.yaml"

server = Test.MakeVerifierServerProcess("h2-pool-origin", replay_file)

ts = Test.MakeATSProcess("ts", enable_tls=True)
ts.addDefaultSSLFiles()
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        # Pin to a single net thread so all transactions land on the same
        # EThread. Cross-thread sharing of multiplexing H/2 origin sessions
        # is intentionally not supported (their state is owned by the
        # EThread driving the connection); reuse is only expected within a
        # single thread, so single-thread is what the test asserts on.
        'proxy.config.exec_thread.limit': 1,
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        # The bug being verified is specific to pool=global: H/2 origin
        # sessions live in the per-thread pool but pool=global only consults
        # the global pool, so reuse silently fails. With the fix in place,
        # `_acquire_session` also checks the thread pool first, so reuse
        # works regardless of the configured pool type.
        'proxy.config.http.server_session_sharing.pool': 'global',
        'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.http.cache.http': 0,
        # Keep the original Host header through remap so SNI matching for
        # session reuse uses the same hostname for every request rather
        # than the IP literal `127.0.0.1` (which TLS will not send as an
        # SNI).
        'proxy.config.url_remap.pristine_host_hdr': 1,
    })

ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.https_port}')
ts.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr = Test.AddTestRun("Drive 5 sequential H/2 requests over a single client session")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client-pool-reuse", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.TimeOut = 60

# `stdout_wait` retries the command until its output matches the gold file
# (or the run times out), so we don't need a separate settling step --
# once ATS finishes releasing all 5 streams the metrics will agree.
tr = Test.AddTestRun("Assert exactly one H/2 origin connection carried all 5 streams")
tr.Processes.Default.Command = (
    f"{Test.Variables.AtsTestToolsDir}/stdout_wait"
    f" 'traffic_ctl metric get"
    f" proxy.process.http2.total_server_connections"
    f" proxy.process.http2.total_server_streams'"
    f" {Test.TestDirectory}/gold/h2o-pool-reuse-metrics.gold")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Regression guard: if the bug is reintroduced, every transaction opens a
# fresh outbound H/2 connection and `Add session to pool` is logged once
# per connection. Catch the case of more than 5 outbound H/2 sessions.
ts.Disk.diags_log.Content = Testers.ExcludesExpression(
    r"(?:.*Add session to pool.*\n.*){5,}.*Add session to pool",
    "must not open more than 5 outbound H/2 origin sessions for 5 requests")
