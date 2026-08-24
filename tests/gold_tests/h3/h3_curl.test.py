'''
Verify HTTP/3 client interop with curl.
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
This test is written specifically to verify that an HTTP/3 curl client can
complete a request through ATS.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_QUIC'),
    Condition.HasCurlFeature('http3'),
    Condition.HasCurlOption('--http3-only'),
)
Test.SkipIf(Condition.CurlUsingUnixDomainSocket())


class TestHttp3Curl:
    """Configure a test to verify HTTP/3 curl client interoperability."""

    response_body = "0123456789" * 30000

    def __init__(self, name: str):
        """Initialize the test.

        :param name: The name of the test.
        """
        self.name = name
        self._body_path = os.path.join(Test.RunDirectory, "h3_curl_body.txt")
        self._configure_server()
        self._configure_traffic_server()
        self._configure_client()

    def _configure_server(self):
        """Configure the origin server."""
        server = Test.MakeOriginServer("server")
        server.addResponse(
            "sessionlog.json", {
                "headers": "GET /h3-curl HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers": f"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: {len(self.response_body)}\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": self.response_body
            })

        self._server = server

    def _configure_traffic_server(self):
        """Configure Traffic Server."""
        ts = Test.MakeATSProcess("ts", enable_tls=True, enable_quic=True, enable_cache=False)
        ts.StartupTimeout = 60
        ts.addDefaultSSLFiles()
        ts.addSSLfile("../tls/ssl/signed-foo.pem")
        ts.addSSLfile("../tls/ssl/signed-foo.key")
        ts.Disk.ssl_multicert_yaml.AddLines(
            '''
ssl_multicert:
  - ssl_cert_name: signed-foo.pem
    ssl_key_name: signed-foo.key
  - ssl_cert_name: server.pem
    ssl_key_name: server.key
    dest_ip: "*"
'''.split('\n'))
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'quic|http3',
                'proxy.config.quic.server.stateless_retry_enabled': 0,
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: h3_access
      format: 'c_alpn=%<cqssa> client_version=%<cqpv> c_ssl_version=%<cqssv> c_method=%<cqhm> c_url=%<cquuc>'

  logs:
    - filename: h3_access
      format: h3_access
'''.split("\n"))

        self._access_log = Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'h3_access.log'), exists=True)
        self._access_log.Content = Testers.ContainsExpression(
            r'c_alpn=h3 client_version=http/3 c_ssl_version=[^ ]+ c_method=GET c_url=https://foo.com:[0-9]+/h3-curl',
            "ATS should log the curl request as HTTP/3")

        self._ts = ts

    def _check_curl_response(self, tr):
        """Verify that curl received the response over HTTP/3."""
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            f"size_download={len(self.response_body)}", "curl should receive the complete HTTP/3 response body")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("http_version=3", "curl should report HTTP/3")

    def _configure_client(self):
        """Configure the curl client test runs."""
        tr = Test.AddTestRun(self.name)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.MakeCurlCommand(
            '--silent --show-error --fail --ipv4 --http3-only --insecure '
            f'--resolve "foo.com:{self._ts.Variables.ssl_port}:127.0.0.1" '
            f'--output "{self._body_path}" '
            '--write-out "\\nhttp_version=%{http_version}\\nsize_download=%{size_download}\\n" '
            f'https://foo.com:{self._ts.Variables.ssl_port}/h3-curl',
            ts=self._ts)
        self._check_curl_response(tr)
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

        tr = Test.AddTestRun("Wait for HTTP/3 access log")
        tr.Processes.Default.Command = (
            os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
            os.path.join(self._ts.Variables.LOGDIR, 'h3_access.log'))
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts


TestHttp3Curl("curl forced HTTP/3 request")
