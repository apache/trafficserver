'''
Verify HTTP/3 range requests over cached content.
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
Verify that HTTP/3 clients can populate cache and receive range responses from
cached objects.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_QUIC'),
    Condition.HasCurlFeature('http3'),
    Condition.HasCurlOption('--http3-only'),
)
Test.SkipIf(Condition.CurlUsingUnixDomainSocket())


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


class TestHttp3RangeCache:
    """Configure an HTTP/3 range-over-cache test."""

    response_body = "0123456789" * 30000
    range_body = "6789012345678901"

    def __init__(self):
        """Initialize the test."""
        self._configure_server()
        self._configure_traffic_server()
        self._configure_clients()

    def _configure_server(self):
        """Configure the origin server."""
        server = Test.MakeOriginServer("server-h3-range-cache")
        server.addResponse(
            "sessionlog.json", {
                "headers": "GET /h3-range-cache HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\n"
                        "Connection: close\r\n"
                        "Cache-Control: public, max-age=60\r\n"
                        f"Content-Length: {len(self.response_body)}\r\n\r\n"),
                "timestamp": "1469733493.993",
                "body": self.response_body
            })
        self._server = server

    def _configure_traffic_server(self):
        """Configure Traffic Server."""
        ts = Test.MakeATSProcess("ts-h3-range-cache", enable_tls=True, enable_quic=True, enable_cache=True)
        ts.StartupTimeout = 60
        ts.addDefaultSSLFiles()
        add_default_ssl_multicert(ts)
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'quic|http3|http',
                'proxy.config.quic.server.stateless_retry_enabled': 0,
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        self._ts = ts

    def _curl_base(self):
        """Build the shared curl arguments."""
        return (
            '--silent --show-error --fail --ipv4 --http3-only --insecure '
            f'--resolve "range.example.com:{self._ts.Variables.ssl_port}:127.0.0.1" '
            f'https://range.example.com:{self._ts.Variables.ssl_port}/h3-range-cache')

    def _configure_clients(self):
        """Configure the cache fill and range request clients."""
        full_body_path = os.path.join(Test.RunDirectory, "h3-range-full.txt")
        range_body_path = os.path.join(Test.RunDirectory, "h3-range-part.txt")

        tr = Test.AddTestRun("HTTP/3 cache fill")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.MakeCurlCommand(
            f'{self._curl_base()} --output "{full_body_path}" '
            '--write-out "\\nhttp_code=%{http_code}\\nsize_download=%{size_download}\\n"',
            ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("http_code=200", "The fill request should return 200.")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            f"size_download={len(self.response_body)}", "The fill request should receive the full object.")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

        tr = Test.AddTestRun("HTTP/3 cached range request")
        tr.MakeCurlCommand(
            f'{self._curl_base()} --header "Range: bytes=16-31" --output "{range_body_path}" '
            '--write-out "\\nhttp_code=%{http_code}\\nsize_download=%{size_download}\\n"',
            ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("http_code=206", "The range request should return 206.")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            "size_download=16", "The range request should receive 16 bytes.")
        Test.Disk.File(
            range_body_path, exists=True).Content = Testers.ContainsExpression(
                self.range_body, "The cached range response body should match the requested byte range.")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts


TestHttp3RangeCache()
