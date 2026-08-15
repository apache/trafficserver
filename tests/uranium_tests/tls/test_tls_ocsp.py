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

from pathlib import Path

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class OcspStaplingScenario:
    """Serve a prefetched OCSP response from ATS's TLS listener."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Serve the request after the client validates the stapled response."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: server.example.com\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Install the certificate, key, chain, and prefetched OCSP response."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            "ssl/ca.ocsp.pem",
            "ssl/server.ocsp.pem",
            "ssl/server.ocsp.key",
            "ssl/ocsp_response.der",
        )
        ats.ssl_multicert_config.add_lines(
            [
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.ocsp.pem",
                "    ssl_key_name: server.ocsp.key",
                "    ssl_ocsp_name: ocsp_response.der",
            ])
        ats.remap_config.add_line(f"map https://server.example.com:{ats.https_port} http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.ssl.server.cert_chain.filename": "ca.ocsp.pem",
                "proxy.config.ssl.ocsp.response.path": str(ats.ssl_directory),
                "proxy.config.ssl.ocsp.enabled": 1,
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl_ocsp",
            })
        return ats

    def run(self) -> None:
        """Require curl to validate the stapled response successfully."""

        self._origin.start()
        self._ats.start()
        result = self._curl.run(
            "--verbose",
            "--cacert",
            str(self._ats.ssl_directory / "ca.ocsp.pem"),
            "--cert-status",
            "--resolve",
            f"server.example.com:{self._ats.https_port}:127.0.0.1",
            f"https://server.example.com:{self._ats.https_port}/",
        )
        assert result.returncode == 0, result.output


def test_tls_ocsp(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS staples its configured prefetched OCSP response."""

    OcspStaplingScenario(ats_factory, services, curl).run()
