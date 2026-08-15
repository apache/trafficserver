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
"""Verify the Proxy Protocol allowlist distinguishes prefaced traffic."""

from pathlib import Path

from tools.uranium.services import ATS, ATSFactory, Curl, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent


class ProxyProtocolAllowlistScenario:
    """Mix ordinary and Proxy-Protocol-prefaced HTTP and TLS connections."""

    REPLAY = TEST_DIRECTORY / "replay" / "proxy_protocol_allowlist.replay.yaml"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._server = self.configure_server(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    @classmethod
    def configure_server(cls, services: ServiceFactory) -> VerifierServer:
        """Create the origin for the two ordinary requests."""

        return services.verifier_server("origin", cls.REPLAY)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Allow Proxy Protocol only from an address other than loopback."""

        ats = ats_factory.create("ats", enable_tls=True, enable_cache=False, enable_proxy_protocol=True)
        ats.add_default_ssl_files()
        ats.remap_config.add_line(f"map / http://127.0.0.1:{self._server.http_port}/")
        ats.records.update(
            {
                "proxy.config.http.proxy_protocol_allowlist": "192.0.2.1",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "proxyprotocol",
            })
        return ats

    def request(self, *, tls: bool, proxy_protocol: bool, uuid: str | None = None) -> int:
        """Issue one request and return curl's status."""

        port = self._ats.proxy_protocol_https_port if tls else self._ats.proxy_protocol_port
        arguments = ["--silent", "--show-error", "--output", "/dev/null", "--max-time", "5"]
        if tls:
            arguments.append("--insecure")
        if proxy_protocol:
            arguments.append("--haproxy-protocol")
        if uuid is not None:
            arguments.extend(("--header", f"uuid: {uuid}"))
        arguments.append(f"{'https' if tls else 'http'}://127.0.0.1:{port}/get")
        return self._curl.run_for(self._ats, *arguments, timeout=10).returncode

    def run(self) -> None:
        """Accept ordinary connections and reject prefaced loopback connections."""

        self._server.start()
        self._ats.start()
        assert self.request(tls=False, proxy_protocol=False, uuid="1") == 0
        assert self.request(tls=True, proxy_protocol=False, uuid="2") == 0
        assert self.request(tls=False, proxy_protocol=True) in (52, 56)
        assert self.request(tls=True, proxy_protocol=True) in (35, 52, 56)


def test_proxy_protocol_allowlist(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The allowlist applies only when a peer sends a Proxy Protocol header."""

    ProxyProtocolAllowlistScenario(ats_factory, services).run()
