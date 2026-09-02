"""Verify that HTTP/2 is not negotiated with prohibited TLS cipher suites."""

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

Test.Summary = __doc__

Test.SkipUnless(Condition.HasOpenSSLVersion("1.1.1"))


class TestH2CipherSuite:
    """Verify HTTP/2 cipher suite restrictions."""

    def __init__(self) -> None:
        """Configure Traffic Server and the cipher suite test runs."""
        self._ts = self._configure_traffic_server()
        self._configure_allowed_cipher_test()

        for description, cipher in (
            ("non-ephemeral AEAD", "AES128-GCM-SHA256"),
            ("ephemeral CBC", "ECDHE-RSA-AES128-SHA256"),
        ):
            self._configure_prohibited_cipher_test(description, cipher)

    def _configure_traffic_server(self) -> 'Process':
        """Configure Traffic Server with allowed and prohibited cipher suites."""
        ts = Test.MakeATSProcess("ts", enable_tls=True)
        ts.addSSLfile("ssl/server.pem")
        ts.addSSLfile("ssl/server.key")

        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        ts.Disk.records_config.update(
            {
                "proxy.config.ssl.server.cert.path": ts.Variables.SSLDir,
                "proxy.config.ssl.server.private_key.path": ts.Variables.SSLDir,
                "proxy.config.ssl.server.version.min": 2,
                "proxy.config.ssl.server.version.max": 2,
                "proxy.config.ssl.server.cipher_suite":
                    "ECDHE-RSA-AES128-GCM-SHA256:"
                    "AES128-GCM-SHA256:"
                    "ECDHE-RSA-AES128-SHA256:"
                    "@SECLEVEL=0",
            })
        return ts

    def _configure_allowed_cipher_test(self) -> None:
        """Verify that an ephemeral AEAD cipher can negotiate HTTP/2."""
        tr = Test.AddTestRun("Allow HTTP/2 with an ephemeral AEAD cipher")
        tr.Processes.Default.Command = (
            "openssl s_client -tls1_2 -cipher ECDHE-RSA-AES128-GCM-SHA256 "
            f"-alpn h2,http/1.1 -connect 127.0.0.1:{self._ts.Variables.ssl_port} </dev/null")
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Streams.All += Testers.IncludesExpression("ALPN protocol: h2", "HTTP/2 should be negotiated")
        tr.ReturnCode = 0
        tr.StillRunningAfter = self._ts

    def _configure_prohibited_cipher_test(self, description: str, cipher: str) -> None:
        """Verify that a prohibited cipher falls back to HTTP/1.1.

        :param description: A human-readable description of the cipher.
        :param cipher: The OpenSSL cipher suite name.
        """
        tr = Test.AddTestRun(f"Fall back to HTTP/1.1 with a prohibited {description} cipher")
        tr.Processes.Default.Command = (
            "printf 'GET / HTTP/1.1\\r\\nHost: example.com\\r\\nConnection: close\\r\\n\\r\\n' | "
            f"openssl s_client -ign_eof -tls1_2 -cipher {cipher} "
            f"-alpn h2,http/1.1 -connect 127.0.0.1:{self._ts.Variables.ssl_port}")
        tr.Processes.Default.Streams.All += Testers.IncludesExpression("ALPN protocol: http/1.1", "HTTP/1.1 should be negotiated")
        tr.Processes.Default.Streams.All += Testers.IncludesExpression("HTTP/1.1 404", "The HTTP/1.1 request should be processed")
        tr.ReturnCode = 0
        tr.StillRunningAfter = self._ts


TestH2CipherSuite()
