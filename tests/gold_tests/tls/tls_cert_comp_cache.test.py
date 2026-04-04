'''
Verify TLS Certificate Compression cache behavior.
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
Verify that the certificate compression cache works correctly. When caching
is enabled, the compressed result is reused across handshakes. When disabled,
each handshake compresses independently and the cache_hit metric stays at 0.
'''

Test.SkipUnless(Condition.HasATSFeature('TS_HAS_CERT_COMPRESSION'))

REPLAY_FILE = 'replay/tls_cert_compression_cache.replay.yaml'


class TestCertCompressionCache:
    server_counter: int = 0
    ts_counter: int = 0
    client_counter: int = 0

    def __init__(self, cache_enabled: bool) -> None:
        self._cache_enabled = cache_enabled
        self._algorithm = 'zlib'
        self._server = self._configure_server()
        self._ts_mid = self._configure_ts_mid()
        self._ts_edge = self._configure_ts_edge()

    def _configure_server(self) -> 'Process':
        name = f'server-{TestCertCompressionCache.server_counter}'
        TestCertCompressionCache.server_counter += 1
        server = Test.MakeVerifierServerProcess(name, REPLAY_FILE)
        return server

    def _configure_ts_mid(self) -> 'Process':
        """Mid-tier ATS that terminates TLS and forwards to origin."""
        name = f'm{TestCertCompressionCache.ts_counter}'
        TestCertCompressionCache.ts_counter += 1
        ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=False)

        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}/')

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.cert_compression.algorithms': self._algorithm,
                'proxy.config.ssl.server.cert_compression.cache': 1 if self._cache_enabled else 0,
                'proxy.config.ssl.server.session_ticket.enable': 0,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl_cert_compress',
            })

        return ts

    def _configure_ts_edge(self) -> 'Process':
        """Edge ATS that connects to mid-tier via HTTPS."""
        name = f'e{TestCertCompressionCache.ts_counter}'
        TestCertCompressionCache.ts_counter += 1
        ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=False)

        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{self._ts_mid.Variables.ssl_port}/')

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.ssl.client.cert_compression.algorithms': self._algorithm,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl_cert_compress',
            })

        return ts

    def run(self) -> None:
        cache_label = 'enabled' if self._cache_enabled else 'disabled'

        # Test run 1: Send 2 requests over 2 separate TLS connections.
        tr = Test.AddTestRun(f'Send 2 requests with cache {cache_label}')
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts_mid)
        tr.Processes.Default.StartBefore(self._ts_edge)

        name = f'client-{TestCertCompressionCache.client_counter}'
        TestCertCompressionCache.client_counter += 1
        tr.AddVerifierClientProcess(name, REPLAY_FILE, http_ports=[self._ts_edge.Variables.port])

        # Test run 2: Check compression count on mid-tier — should be 2.
        tr = Test.AddTestRun(f'Verify compression count with cache {cache_label}')
        tr.Processes.Default.Command = (f'traffic_ctl metric get'
                                        f' proxy.process.ssl.cert_compress.{self._algorithm}')
        tr.Processes.Default.Env = self._ts_mid.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'proxy.process.ssl.cert_compress.{self._algorithm} 2', f'Should have 2 {self._algorithm} compressions')
        tr.StillRunningAfter = self._ts_mid
        tr.StillRunningAfter = self._ts_edge
        tr.StillRunningAfter = self._server

        # Test run 3: Check decompression count on edge — should be 2.
        tr = Test.AddTestRun(f'Verify decompression count with cache {cache_label}')
        tr.Processes.Default.Command = (f'traffic_ctl metric get'
                                        f' proxy.process.ssl.cert_decompress.{self._algorithm}')
        tr.Processes.Default.Env = self._ts_edge.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'proxy.process.ssl.cert_decompress.{self._algorithm} 2', f'Should have 2 {self._algorithm} decompressions')
        tr.StillRunningAfter = self._ts_mid
        tr.StillRunningAfter = self._ts_edge
        tr.StillRunningAfter = self._server

        # Test run 4: Verify no compression failures.
        tr = Test.AddTestRun(f'Verify no compression failures with cache {cache_label}')
        tr.Processes.Default.Command = (f'traffic_ctl metric get'
                                        f' proxy.process.ssl.cert_compress.{self._algorithm}_failure')
        tr.Processes.Default.Env = self._ts_mid.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'proxy.process.ssl.cert_compress.{self._algorithm}_failure 0',
            f'Should have no {self._algorithm} compression failures')
        tr.StillRunningAfter = self._ts_mid
        tr.StillRunningAfter = self._ts_edge
        tr.StillRunningAfter = self._server

        # Test run 5: Check cache_hit metric.
        if not self._cache_enabled:
            tr = Test.AddTestRun(f'Verify cache_hit is 0 with cache {cache_label}')
            tr.Processes.Default.Command = 'traffic_ctl metric get proxy.process.ssl.cert_compress.cache_hit'
            tr.Processes.Default.Env = self._ts_mid.Env
            tr.Processes.Default.ReturnCode = 0
            tr.Processes.Default.Streams.All = Testers.ContainsExpression(
                'proxy.process.ssl.cert_compress.cache_hit 0', 'cache_hit should be 0 when caching is disabled')
            tr.StillRunningAfter = self._ts_mid
            tr.StillRunningAfter = self._ts_edge
            tr.StillRunningAfter = self._server


TestCertCompressionCache(cache_enabled=True).run()
TestCertCompressionCache(cache_enabled=False).run()
