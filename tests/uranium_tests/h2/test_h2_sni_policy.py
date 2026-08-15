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

import re

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


class H2SniPolicyScenario:
    """Verify SNI policy can enable or disable HTTP/2 negotiation."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        curl: Curl,
        *,
        sni_enables_h2: bool,
        accept_threads: int,
    ) -> None:
        if not Curl.supports("http2"):
            pytest.skip("curl lacks HTTP/2 support")
        self._curl = curl
        self._sni_enables_h2 = sni_enables_h2
        self._origin = self.configure_origin(services)
        self._ats = self.configure_ats(ats_factory, accept_threads)

    def configure_origin(self, services: ServiceFactory) -> OriginServer:
        """Configure the shared empty origin response."""

        origin = services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\n\r\n",
                "body": ""
            },
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": ""
            },
        )
        return origin

    def configure_ats(self, ats_factory: ATSFactory, accept_threads: int) -> ATS:
        """Configure the global protocol and opposing SNI policy."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http|ssl",
                "proxy.config.url_remap.pristine_host_hdr": 1,
                "proxy.config.accept_threads": accept_threads,
            })
        if self._sni_enables_h2:
            ats.records.update({
                "proxy.config.http.server_ports": f"{ats.https_port}:ssl:proto=http {ats.http_port}",
            })
        state = "on" if self._sni_enables_h2 else "off"
        ats.write_config_file("sni.yaml", f'''sni:
- fqdn: bar.com
  http2: {state}
- fqdn: "*.foo.com"
  http2: {state}
''')
        return ats

    def request(self, hostname: str, expects_h2: bool) -> None:
        """Connect with one SNI name and verify the negotiated protocol."""

        result = self._curl.run_for(
            self._ats,
            "--verbose",
            "--insecure",
            "--ipv4",
            "--resolve",
            f"{hostname}:{self._ats.https_port}:127.0.0.1",
            f"https://{hostname}:{self._ats.https_port}/",
            timeout=10,
        )
        assert result.returncode == 0, result.output
        assert "Could Not Connect" not in result.output
        negotiated_h2 = re.search(r"using HTTP/?2", result.output, re.IGNORECASE) is not None
        assert negotiated_h2 is expects_h2, result.output

    def run(self) -> None:
        """Compare the global default, exact SNI, and wildcard SNI behavior."""

        self._origin.start()
        self._ats.start()
        self.request("foo.com", not self._sni_enables_h2)
        self.request("bar.com", self._sni_enables_h2)
        self.request("bob.foo.com", self._sni_enables_h2)


@pytest.mark.parametrize("accept_threads", [0, 1], ids=["net-thread-accept", "dedicated-accept-thread"])
@pytest.mark.parametrize("sni_enables_h2", [False, True], ids=["sni-disables-h2", "sni-enables-h2"])
def test_h2_sni_policy(
    ats_factory: ATSFactory,
    services: ServiceFactory,
    curl: Curl,
    sni_enables_h2: bool,
    accept_threads: int,
) -> None:
    """SNI HTTP/2 policy works with either listener acceptance model."""

    H2SniPolicyScenario(
        ats_factory,
        services,
        curl,
        sni_enables_h2=sni_enables_h2,
        accept_threads=accept_threads,
    ).run()
