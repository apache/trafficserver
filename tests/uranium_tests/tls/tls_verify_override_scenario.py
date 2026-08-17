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
import time

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, DNSServer, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


class TlsVerifyOverrideScenario:
    """Exercise per-remap outbound certificate verification and SNI policy."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        *,
        include_server_name: bool,
    ) -> None:
        self._curl = curl
        self._include_server_name = include_server_name
        self._foo = self.configure_origin(services, "foo-origin", "signed-foo")
        self._bar = self.configure_origin(services, "bar-origin", "signed-bar")
        self._untrusted = services.origin("untrusted-origin", ssl=True)
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_origin(services: ServiceFactory, name: str, certificate: str) -> OriginServer:
        """Create an HTTPS origin using one certificate signed by the test CA."""

        origin = services.origin(
            name,
            ssl=True,
            clientkey=SSL_DIRECTORY / f"{certificate}.key",
            clientcert=SSL_DIRECTORY / f"{certificate}.pem",
        )
        origin.add_response(
            {"headers": "GET / HTTP/1.1\r\nHost: ignored\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
        )
        return origin

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve every remap destination to the local test origins."""

        dns = services.dns("dns")
        dns.add_records({"foo.com": ["127.0.0.1"], "bar.com": ["127.0.0.1"], "random.com": ["127.0.0.1"]})
        return dns

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure permissive global verification plus strict remap overrides."""

        ats = ats_factory.create("ts", enable_tls=self._include_server_name)
        ats.copy_to_ssl(
            SSL_DIRECTORY / "signed-foo.pem",
            SSL_DIRECTORY / "signed-foo.key",
            SSL_DIRECTORY / "signed-bar.pem",
            SSL_DIRECTORY / "signed-bar.key",
            SSL_DIRECTORY / "server.pem",
            SSL_DIRECTORY / "server.key",
            SSL_DIRECTORY / "signer.pem",
            SSL_DIRECTORY / "signer.key",
        )
        ats.set_ssl_multicert_yaml(
            {"ssl_multicert": [{
                "dest_ip": "*",
                "ssl_cert_name": "server.pem",
                "ssl_key_name": "server.key"
            }]})
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "ssl",
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.client.CA.cert.filename": "signer.pem",
                "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.http.connect.down.policy": 1,
            })
        if not self._include_server_name:
            ats.records.update(
                {
                    "proxy.config.ssl.client.verify.server.properties": "ALL",
                    "proxy.config.ssl.client.sni_policy": "remap",
                })
        self.configure_remap(ats)
        return ats

    def configure_remap(self, ats: ATS) -> None:
        """Install the remap-specific verification and outbound SNI matrix."""

        plugin = "@plugin=conf_remap.so"
        policy = "@pparam=proxy.config.ssl.client.verify.server.policy"
        properties = "@pparam=proxy.config.ssl.client.verify.server.properties"
        sni_policy = "@pparam=proxy.config.ssl.client.sni_policy"
        if not self._include_server_name:
            ats.remap_config.add_line(f"map http://foo.com/basictobar https://bar.com:{self._bar.https_port}")
        ats.remap_config.add_line(f"map http://foo.com/basic https://foo.com:{self._foo.https_port}")
        ats.remap_config.add_line(f"map http://foo.com/override https://foo.com:{self._foo.https_port} {plugin} {policy}=ENFORCED")
        ats.remap_config.add_line(f"map http://bar.com/basic https://bar.com:{self._foo.https_port}")
        ats.remap_config.add_line(
            f"map http://bar.com/overridedisabled https://bar.com:{self._foo.https_port} "
            f"{plugin} {policy}=DISABLED")
        if not self._include_server_name:
            ats.remap_config.add_line(
                f"map http://bad_bar.com/overridedisabled https://bar.com:{self._foo.https_port} "
                f"{plugin} {policy}=DISABLED")
        ats.remap_config.add_line(
            f"map http://bar.com/overridesignature https://bar.com:{self._foo.https_port} "
            f"{plugin} {properties}=SIGNATURE {plugin} {policy}=ENFORCED")
        if not self._include_server_name:
            ats.remap_config.add_line(
                f"map http://bar.com/overridenone https://bar.com:{self._foo.https_port} "
                f"{plugin} {properties}=NONE {plugin} {policy}=ENFORCED")
        ats.remap_config.add_line(
            f"map http://bar.com/overrideenforced https://bar.com:{self._foo.https_port} "
            f"{plugin} {policy}=ENFORCED")
        basic_host = "127.0.0.1" if self._include_server_name else "random.com"
        ats.remap_config.add_line(f"map /basic https://{basic_host}:{self._untrusted.https_port}")
        ats.remap_config.add_line(f"map /overrideenforce https://127.0.0.1:{self._untrusted.https_port} {plugin} {policy}=ENFORCED")
        ats.remap_config.add_line(f"map /overridename https://127.0.0.1:{self._untrusted.https_port} {plugin} {properties}=NAME")
        for origin_name in ("foo", "bar"):
            for mode in ("remap", "host"):
                ats.remap_config.add_line(
                    f"map /snipolicy{origin_name}{mode} https://{origin_name}.com:{self._bar.https_port} "
                    f"{plugin} {properties}=NAME {plugin} {policy}=ENFORCED {plugin} {sni_policy}={mode}")
            if self._include_server_name:
                ats.remap_config.add_line(
                    f"map /snipolicy{origin_name}servername https://{origin_name}.com:{self._bar.https_port} "
                    f"{plugin} {properties}=NAME {plugin} {policy}=ENFORCED {plugin} {sni_policy}=server_name")

    def request(self, host: str, path: str, *, inbound_tls: bool = False) -> CommandResult:
        """Send one request through ATS while preserving its HTTP Host or TLS SNI."""

        if inbound_tls:
            return self._curl.run_for(
                self._ats,
                (
                    f"--insecure --verbose --resolve '{host}:{self._ats.https_port}:127.0.0.1' "
                    f"'https://{host}:{self._ats.https_port}/{path}'"),
            )
        return self._curl.get(
            self._ats,
            path,
            headers={"Host": host},
            options=f"--insecure --verbose",
        )

    @staticmethod
    def assert_connected(result: CommandResult, expected_status: str = "200 OK") -> None:
        """Require ATS to establish the configured origin connection."""

        assert result.returncode == 0, result.output
        assert "Could Not Connect" not in result.output
        status = expected_status.split()[0]
        assert f"HTTP/1.1 {status}" in result.output or f"HTTP/2 {status}" in result.output, result.output

    @staticmethod
    def assert_rejected(result: CommandResult) -> None:
        """Require ATS to reject the outbound TLS connection."""

        assert result.returncode == 0, result.output
        assert "Could Not Connect" in result.output

    def verify_requests(self) -> None:
        """Run the common policy checks and the variant-specific SNI cases."""

        self.assert_connected(self.request("foo.com", "basic"))
        self.assert_connected(self.request("bar.com", "basic"))
        self.assert_connected(self.request("random.com", "basic"), "404 Not Found")
        if not self._include_server_name:
            self.assert_connected(self.request("foo.com", "basictobar"))
        self.assert_connected(self.request("foo.com", "override"))
        disabled_host = "bar.com" if self._include_server_name else "bad_bar.com"
        self.assert_connected(self.request(disabled_host, "overridedisabled"))
        self.assert_connected(self.request("bar.com", "overridesignature"))
        if not self._include_server_name:
            self.assert_connected(self.request("bar.com", "overridenone"))
        self.assert_rejected(self.request("bar.com", "overrideenforced"))

        self.assert_connected(self.request("foo.com", "snipolicybarremap"))
        self.assert_rejected(self.request("foo.com", "snipolicybarhost"))
        self.assert_rejected(self.request("bar.com", "snipolicyfooremap"))
        self.assert_connected(self.request("bar.com", "snipolicyfoohost"))
        if self._include_server_name:
            self.assert_rejected(self.request("foo.com", "snipolicybarservername", inbound_tls=True))
            self.assert_connected(self.request("bar.com", "snipolicyfooservername", inbound_tls=True))

    def verify_diagnostics(self) -> None:
        """Check permissive and enforced certificate failures in diags.log."""

        diagnostics = ""
        for _ in range(100):
            diagnostics = self._ats.diags_log.read_text(errors="replace") if self._ats.diags_log.exists() else ""
            if "SNI (bar.com) not in certificate. Action=Terminate" in diagnostics:
                break
            time.sleep(0.05)
        else:
            raise AssertionError(f"Expected outbound TLS verification diagnostics:\n{diagnostics}")

        assert "Action=Continue Error=self-signed certificate" in diagnostics
        assert "SNI (bar.com) not in certificate. Action=Continue" in diagnostics
        assert "SNI (random.com) not in certificate. Action=Continue" in diagnostics
        assert "SNI (bar.com) not in certificate. Action=Terminate" in diagnostics
        if not self._include_server_name:
            assert "SNI (foo.com) not in certificate. Action=Continue" not in diagnostics

    def run(self) -> None:
        """Start the native services, execute the matrix, and inspect diagnostics."""

        self._dns.start()
        self._foo.start()
        self._bar.start()
        self._untrusted.start()
        self._ats.start()
        self.verify_requests()
        self.verify_diagnostics()
