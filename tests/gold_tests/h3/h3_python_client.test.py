'''
Verify HTTP/3 client interop with an aioquic Python client.
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
import sys

Test.Summary = '''
Verify that an aioquic HTTP/3 client can complete normal requests and selected
HTTP/3 edge-case probes through ATS.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_QUIC'),
    Condition.HasProgram("python3", "python3 is required for the aioquic HTTP/3 client"),
)


def add_default_ssl_multicert(ts):
    """Configure the default server certificate."""
    if hasattr(ts.Disk, "ssl_multicert_yaml"):
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
    else:
        ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")


class TestHttp3PythonClient:
    """Configure a test to verify HTTP/3 aioquic client interoperability."""

    replay_file = "replays/h3_server_for_python_client.replay.yaml"

    def __init__(self, name: str):
        """Initialize the test."""
        self.name = name
        self._configure_server()
        self._configure_traffic_server()
        self._configure_client()

    def _configure_server(self):
        """Configure the Proxy Verifier origin server."""
        self._server = Test.MakeVerifierServerProcess(
            "server-python-h3-client", self.replay_file, verbose=False, other_args="--poll-timeout 30000")

    def _configure_traffic_server(self):
        """Configure Traffic Server."""
        ts = Test.MakeATSProcess("ts-python-h3-client", enable_tls=True, enable_quic=True, enable_cache=False)
        ts.StartupTimeout = 60
        ts.addDefaultSSLFiles()
        add_default_ssl_multicert(ts)
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'quic|http3',
                'proxy.config.quic.initial_max_data_in': 1000000,
                'proxy.config.quic.initial_max_stream_data_bidi_remote_in': 1000000,
                'proxy.config.quic.max_send_udp_payload_size_in': 1200,
                'proxy.config.quic.server.stateless_retry_enabled': 0,
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')
        ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: h3_python_access
      format: 'c_alpn=%<cqssa> client_version=%<cqpv> c_method=%<cqhm> c_url=%<cquuc>'

  logs:
    - filename: h3_python_access
      format: h3_python_access
'''.split("\n"))

        self._access_log = Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'h3_python_access.log'), exists=True)
        self._access_log.Content = Testers.ContainsExpression(
            r'c_alpn=h3 client_version=http/3 c_method=GET c_url=https://py\.example\.com:[0-9]+/py-get-empty',
            "ATS should log the aioquic request as HTTP/3")
        self._access_log.Content += Testers.ContainsExpression(
            r'c_alpn=h3 client_version=http/3 c_method=PUT c_url=https://py\.example\.com:[0-9]+/py-put-large',
            "ATS should log the aioquic large PUT as HTTP/3")

        self._ts = ts

    def _configure_client(self):
        """Configure the aioquic client test runs."""
        tr = Test.AddTestRun(self.name)
        tr.Setup.Copy("py_h3_client")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        client_dir = os.path.join(tr.RunDirectory, "py_h3_client")
        tr.Processes.Default.Command = (
            f'"{sys.executable}" "{os.path.join(client_dir, "h3_client.py")}" '
            f'--addr 127.0.0.1:{self._ts.Variables.ssl_port} '
            f'--authority py.example.com:{self._ts.Variables.ssl_port} '
            '--server-name py.example.com')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            "completed 18 Python HTTP/3 checks", "The aioquic client should complete all HTTP/3 checks.")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

        tr = Test.AddTestRun("Wait for aioquic HTTP/3 access log")
        tr.Processes.Default.Command = (
            os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
            os.path.join(self._ts.Variables.LOGDIR, 'h3_python_access.log'))
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts


TestHttp3PythonClient("aioquic HTTP/3 client requests")
