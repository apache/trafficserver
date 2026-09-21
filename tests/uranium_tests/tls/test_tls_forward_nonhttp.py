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

from tools.uranium.services import ATS, ATSFactory, DNSServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


class ForwardNonHttpScenario:
    """Terminate client TLS and forward its byte stream to a TCP service."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._forward_port = services.allocate_port()
        self._dns = self.configure_dns(services)
        self._ats = self.configure_ats(ats_factory)
        self._client = self.configure_client(services)

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Resolve the SNI forward route locally."""

        return services.dns("dns", default="127.0.0.1")

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Route the bar.com TLS stream to the raw TCP listener."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.records.update(
            {
                "proxy.config.http.connect_ports": f"{ats.https_port} {self._forward_port}",
                "proxy.config.exec_thread.autoconfig.scale": 1.0,
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
        ats.allow_private_connect()
        ats.write_config_file(
            "sni.yaml",
            f"sni:\n- fqdn: bar.com\n  forward_route: localhost:{self._forward_port}\n",
        )
        return ats

    def configure_client(self, services: ServiceFactory) -> ProcessService:
        """Run the paired netcat server and OpenSSL client."""

        return services.process(
            "non-http-client",
            (
                "sh",
                TEST_DIRECTORY / "test-nc-s_client.sh",
                str(self._forward_port),
                str(self._ats.https_port),
            ),
        )

    def run(self) -> None:
        """Execute the tunneled exchange and require the raw reply."""

        self._dns.start()
        self._ats.start()
        result = self._client.run(timeout=30)
        assert result.returncode == 0, result.output
        assert "This is a reply" in result.output, result.output + self._ats.diags_log.read_text(errors="replace")


def test_tls_forward_nonhttp(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """SNI forward_route carries a non-HTTP protocol out of TLS."""

    ForwardNonHttpScenario(ats_factory, services).run()
