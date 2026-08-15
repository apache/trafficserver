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

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class VerifyNonPristineHostScenario:
    """Verify an origin certificate against the remapped, not pristine, host."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._curl = curl
        self._origin = self.configure_origin(services)
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory) -> OriginServer:
        """Present a `foo.com` certificate from the TLS origin."""

        origin = services.origin(
            "origin",
            ssl=True,
            clientkey=TEST_DIRECTORY / "ssl" / "signed-foo.key",
            clientcert=TEST_DIRECTORY / "ssl" / "signed-foo.pem",
        )
        response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": ""}
        for hostname in ("foo.com", "badfoo.com"):
            origin.add_response(
                {
                    "headers": f"GET / HTTP/1.1\r\nHost: {hostname}\r\n\r\n",
                    "body": ""
                },
                response,
            )
        return origin

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve both remap hosts to the test origin."""

        dns = services.dns("dns")
        dns.add_records({"foo.com": ["127.0.0.1"], "bar.com": ["127.0.0.1"]})
        return dns

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enforce signature and hostname checks against remapped hosts."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.copy_to_ssl(
            *(
                TEST_DIRECTORY / "ssl" / name for name in (
                    "signed-foo.pem",
                    "signed-foo.key",
                    "signed-bar.pem",
                    "signed-bar.key",
                    "server.pem",
                    "server.key",
                    "signer.pem",
                    "signer.key",
                )))
        ats.remap_config.add_lines(
            (
                f"map https://bar.com:{ats.https_port}/ https://foo.com:{self._origin.https_port}",
                f"map https://foo.com:{ats.https_port}/ https://bar.com:{self._origin.https_port}",
            ))
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.records.update(
            {
                "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
                "proxy.config.ssl.client.verify.server.properties": "ALL",
                "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
                "proxy.config.url_remap.pristine_host_hdr": 0,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.dns.resolv_conf": "NULL",
            })
        return ats

    def request(self, hostname: str) -> str:
        """Send one client request and return verbose curl output."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--resolve",
            f"{hostname}:{self._ats.https_port}:127.0.0.1",
            "--insecure",
            f"https://{hostname}:{self._ats.https_port}",
        )
        assert result.returncode == 0, result.output
        return result.output

    def run(self) -> None:
        """Accept the matching remap and reject the mismatching remap."""

        self._origin.start()
        self._dns.start()
        self._ats.start()
        assert "200" in self.request("bar.com")
        failure = self.request("foo.com")
        assert "Could Not Connect" in failure or "502" in failure
        diags = wait_for_file_lines(self._ats.diags_log, r"WARNING: SNI \(bar.com\) not in certificate", 1)
        assert "verification failed" not in diags


def test_tls_verify_not_pristine(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Origin hostname verification uses the non-pristine remap target."""

    VerifyNonPristineHostScenario(ats_factory, services, curl).run()
