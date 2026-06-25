'''
Verify HTTP/3 client interop with a quic-go client.
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under
#  the Apache License, Version 2.0 (the "License"); you may not
#  use this file except in compliance with the License.  You may
#  obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import os

Test.Summary = '''
Verify that a quic-go HTTP/3 client can complete sequential and concurrent
transactions through ATS.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_QUIC'),
    Condition.HasGoVersion('1.24'),
)


def add_default_ssl_multicert(ts):
    """Configure the default server certificate."""
    ts.Disk.ssl_multicert_yaml.AddLines(
        """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))


class TestHttp3GoClient:
    """Configure a test to verify HTTP/3 quic-go client interoperability."""

    replay_file = "replays/h3_server_for_go_client.replay.yaml"

    def __init__(self, name: str):
        """Initialize the test."""
        self.name = name
        self._configure_server()
        self._configure_traffic_server()
        self._configure_client()

    def _configure_server(self):
        """Configure the Proxy Verifier origin server."""
        self._server = Test.MakeVerifierServerProcess(
            "server-go-h3-client", self.replay_file, verbose=False, other_args="--poll-timeout 30000")

    def _configure_traffic_server(self):
        """Configure Traffic Server."""
        ts = Test.MakeATSProcess("ts-go-h3-client", enable_tls=True, enable_quic=True, enable_cache=False)
        ts.StartupTimeout = 60
        ts.addDefaultSSLFiles()
        add_default_ssl_multicert(ts)
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'quic|http3',
                'proxy.config.quic.initial_max_data_in': 1000000,
                'proxy.config.quic.initial_max_stream_data_bidi_remote_in': 1000000,
                'proxy.config.quic.server.stateless_retry_enabled': 0,
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')
        ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: h3_go_access
      format: 'c_alpn=%<cqssa> client_version=%<cqpv> c_method=%<cqhm> c_url=%<cquuc>'

  logs:
    - filename: h3_go_access
      format: h3_go_access
'''.split("\n"))

        self._access_log = Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'h3_go_access.log'), exists=True)
        self._access_log.Content = Testers.ContainsExpression(
            r'c_alpn=h3 client_version=http/3 c_method=GET c_url=https://go\.example\.com:[0-9]+/go-get-empty',
            "ATS should log the quic-go request as HTTP/3")
        self._access_log.Content += Testers.ContainsExpression(
            r'c_alpn=h3 client_version=http/3 c_method=POST c_url=https://go\.example\.com:[0-9]+/go-post-large',
            "ATS should log the quic-go large POST as HTTP/3")

        self._ts = ts

    def _configure_client(self):
        """Configure the quic-go client test runs."""
        tr = Test.AddTestRun(self.name)
        tr.Setup.Copy("go_h3_client")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Env['GOFLAGS'] = '-mod=readonly'
        tr.Processes.Default.Env['GOCACHE'] = os.path.join(tr.RunDirectory, 'gocache')
        tr.Processes.Default.Env['GOMODCACHE'] = os.path.join(tr.RunDirectory, 'gomodcache')
        tr.Processes.Default.Env['GOTOOLCHAIN'] = 'local'
        tr.Processes.Default.Command = (
            f'cd "{os.path.join(tr.RunDirectory, "go_h3_client")}" && '
            f'go run . --addr 127.0.0.1:{self._ts.Variables.ssl_port} '
            f'--authority go.example.com:{self._ts.Variables.ssl_port} '
            '--server-name go.example.com')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            "completed 13 HTTP/3 requests", "The quic-go client should complete all HTTP/3 requests.")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

        tr = Test.AddTestRun("Wait for quic-go HTTP/3 access log")
        tr.Processes.Default.Command = (
            os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
            os.path.join(self._ts.Variables.LOGDIR, 'h3_go_access.log'))
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts


TestHttp3GoClient("quic-go HTTP/3 client requests")
