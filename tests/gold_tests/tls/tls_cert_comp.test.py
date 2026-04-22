'''
Verify TLS Certificate Compression (RFC 8879) between two ATS processes.
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
Verify TLS Certificate Compression (RFC 8879) works between two ATS
instances. An edge ATS (client) connects via HTTPS to a mid ATS (server)
with cert compression enabled. The test verifies compression and
decompression succeed by checking the ssl cert compression metrics.
'''

Test.SkipUnless(Condition.HasATSFeature('TS_HAS_CERT_COMPRESSION'))

REPLAY_FILE = 'replay/tls_cert_compression.replay.yaml'


class TestCertCompression:
    server_counter: int = 0
    ts_counter: int = 0
    client_counter: int = 0

    def __init__(self, algorithm: str) -> None:
        self._algorithm = algorithm
        self._server = self._configure_server()
        self._ts_mid = self._configure_ts_mid()
        self._ts_edge = self._configure_ts_edge()

    def _configure_server(self) -> 'Process':
        name = f'server-{TestCertCompression.server_counter}'
        TestCertCompression.server_counter += 1
        server = Test.MakeVerifierServerProcess(name, REPLAY_FILE)
        return server

    def _configure_ts_mid(self) -> 'Process':
        """Mid-tier ATS that terminates TLS and forwards to origin."""
        name = f'm{TestCertCompression.ts_counter}'
        TestCertCompression.ts_counter += 1
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
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl_cert_compress',
            })

        return ts

    def _configure_ts_edge(self) -> 'Process':
        """Edge ATS that connects to mid-tier via HTTPS."""
        name = f'e{TestCertCompression.ts_counter}'
        TestCertCompression.ts_counter += 1
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
        # Test run 1: Send traffic through the proxy chain.
        tr = Test.AddTestRun(f'Send request through edge->mid with {self._algorithm} cert compression')
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts_mid)
        tr.Processes.Default.StartBefore(self._ts_edge)

        name = f'client-{TestCertCompression.client_counter}'
        TestCertCompression.client_counter += 1
        tr.AddVerifierClientProcess(name, REPLAY_FILE, http_ports=[self._ts_edge.Variables.port])

        # Test run 2: Check compression metric on the mid-tier (server side).
        tr = Test.AddTestRun(f'Verify {self._algorithm} compression metric on mid-tier')
        tr.Processes.Default.Command = (f'traffic_ctl metric get'
                                        f' proxy.process.ssl.cert_compress.{self._algorithm}')
        tr.Processes.Default.Env = self._ts_mid.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'proxy.process.ssl.cert_compress.{self._algorithm} 1',
            f'Certificate should have been compressed with {self._algorithm}')
        tr.StillRunningAfter = self._ts_mid
        tr.StillRunningAfter = self._ts_edge
        tr.StillRunningAfter = self._server

        # Test run 3: Check decompression metric on the edge (client side).
        tr = Test.AddTestRun(f'Verify {self._algorithm} decompression metric on edge')
        tr.Processes.Default.Command = (f'traffic_ctl metric get'
                                        f' proxy.process.ssl.cert_decompress.{self._algorithm}')
        tr.Processes.Default.Env = self._ts_edge.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'proxy.process.ssl.cert_decompress.{self._algorithm} 1',
            f'Certificate should have been decompressed with {self._algorithm}')
        tr.StillRunningAfter = self._ts_mid
        tr.StillRunningAfter = self._ts_edge
        tr.StillRunningAfter = self._server

        # Test run 4: Verify no failures on either side.
        tr = Test.AddTestRun(f'Verify no {self._algorithm} compression failures on mid-tier')
        tr.Processes.Default.Command = (f'traffic_ctl metric get'
                                        f' proxy.process.ssl.cert_compress.{self._algorithm}_failure')
        tr.Processes.Default.Env = self._ts_mid.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'proxy.process.ssl.cert_compress.{self._algorithm}_failure 0',
            f'There should be no {self._algorithm} compression failures')
        tr.StillRunningAfter = self._ts_mid
        tr.StillRunningAfter = self._ts_edge
        tr.StillRunningAfter = self._server

        tr = Test.AddTestRun(f'Verify no {self._algorithm} decompression failures on edge')
        tr.Processes.Default.Command = (f'traffic_ctl metric get'
                                        f' proxy.process.ssl.cert_decompress.{self._algorithm}_failure')
        tr.Processes.Default.Env = self._ts_edge.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'proxy.process.ssl.cert_decompress.{self._algorithm}_failure 0',
            f'There should be no {self._algorithm} decompression failures')
        tr.StillRunningAfter = self._ts_mid
        tr.StillRunningAfter = self._ts_edge
        tr.StillRunningAfter = self._server


algorithms = ['zlib']
if Condition.HasATSFeature('TS_HAS_BROTLI'):
    algorithms.append('brotli')
if Condition.HasATSFeature('TS_HAS_ZSTD'):
    algorithms.append('zstd')
for algorithm in algorithms:
    TestCertCompression(algorithm).run()
