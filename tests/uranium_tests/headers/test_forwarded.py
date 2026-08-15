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
"""Verify Forwarded header configuration for HTTP versions and transports."""

import time

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold, wait_for_file_lines


class ForwardedScenario:
    """Configure an observing origin and two ATS instances."""

    SINGLE_OPTIONS = (
        ("www.forwarded-none.com", "none"),
        ("www.forwarded-for.com", "for"),
        ("www.forwarded-by-ip.com", "by=ip"),
        ("www.forwarded-by-unknown.com", "by=unknown"),
        ("www.forwarded-by-server-name.com", "by=serverName"),
        ("www.forwarded-by-uuid.com", "by=uuid"),
        ("www.forwarded-proto.com", "proto"),
        ("www.forwarded-host.com", "host"),
        ("www.forwarded-connection-compact.com", "connection=compact"),
        ("www.forwarded-connection-std.com", "connection=std"),
        ("www.forwarded-connection-full.com", "connection=full"),
    )
    ALL_OPTIONS = "for|by=ip|by=unknown|by=servername|by=uuid|proto|host|connection=compact|connection=std|connection=full"

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._curl = Curl(ats_factory.run_directory)
        self._origin = self.configure_server()
        self._remap_ats = self.configure_remap_ats()
        self._global_ats = self.configure_global_ats()

    def configure_server(self) -> OriginServer:
        """Create an origin hook that records each Forwarded field."""

        origin = self._services.origin(
            "origin",
            options={"--load": self._services.resolve_path("forwarded-observer.py")},
        )
        hosts = ("www.no-oride.com", *(host for host, _option in self.SINGLE_OPTIONS))
        for host in hosts:
            origin.add_response(
                {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
                {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
            )
        return origin

    def configure_baseline(self, ats: ATS) -> None:
        """Apply shared TLS, records, and baseline remapping."""

        ats.add_default_ssl_files()
        ats.records.update({
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.proxy_name": "Poxy_Proxy",
        })
        ats.remap_config.add_line(f"map http://www.no-oride.com http://127.0.0.1:{self._origin.port}")

    def configure_remap_ats(self) -> ATS:
        """Configure one mapping for each insert_forwarded value."""

        ats = self._ats_factory.create("remap-ats", enable_tls=True, enable_cache=False)
        self.configure_baseline(ats)
        for host, option in self.SINGLE_OPTIONS:
            ats.remap_config.add_line(
                f"map http://{host} http://127.0.0.1:{self._origin.port} "
                f"@plugin=conf_remap.so @pparam=proxy.config.http.insert_forwarded={option}")
        return ats

    def configure_global_ats(self) -> ATS:
        """Configure global Forwarded settings plus IPv4 and IPv6 listeners."""

        ats = self._ats_factory.create("global-ats", enable_tls=True, enable_cache=False)
        self.configure_baseline(ats)
        ats.records.update(
            {
                "proxy.config.http.insert_forwarded": "by=uuid",
                "proxy.config.http.server_ports":
                    (f"{ats.http_port} {ats.ipv6_port}:ipv6 "
                     f"{ats.https_port}:ssl {ats.ipv6_https_port}:ssl:ipv6"),
            })
        ats.remap_config.add_line(f"map https://www.no-oride.com http://127.0.0.1:{self._origin.port}")
        return ats

    def curl(self, ats: ATS, *arguments: str) -> None:
        """Run one curl request and require successful transport."""

        result = self._curl.run_for(ats, "--silent", "--show-error", "--verbose", *arguments)
        assert result.returncode == 0, result.output

    def run_remap_requests(self) -> None:
        """Exercise absent and per-remap Forwarded settings in observer order."""

        proxy = f"localhost:{self._remap_ats.http_port}"
        self.curl(self._remap_ats, "--ipv4", "--http1.1", "--proxy", proxy, "http://www.no-oride.com")
        for host, _option in self.SINGLE_OPTIONS:
            self.curl(self._remap_ats, "--ipv4", "--http1.1", "--proxy", proxy, f"http://{host}")

    def enable_all_options(self) -> None:
        """Change the global record before the protocol matrix."""

        result = self._global_ats.traffic_ctl("config", "set", "proxy.config.http.insert_forwarded", self.ALL_OPTIONS)
        assert result.returncode == 0, result.output
        time.sleep(15)

    def run_global_requests(self) -> None:
        """Exercise global settings over HTTP/1.0, HTTP/1.1, HTTP/2, TLS, and IPv6."""

        ats = self._global_ats
        proxy4 = f"localhost:{ats.http_port}"
        self.curl(ats, "--ipv4", "--http1.1", "--proxy", proxy4, "http://www.no-oride.com")
        self.enable_all_options()
        self.curl(ats, "--ipv4", "--http1.1", "--proxy", proxy4, "http://www.no-oride.com")
        self.curl(ats, "--ipv4", "--http1.0", "--proxy", proxy4, "http://www.no-oride.com")
        self.curl(
            ats,
            "--header",
            "forwarded:for=0.6.6.6",
            "--header",
            "forwarded:for=_argh",
            "--ipv4",
            "--http1.0",
            "--proxy",
            proxy4,
            "http://www.no-oride.com",
        )
        common_tls = ("--insecure", "--header", "Host: www.no-oride.com")
        self.curl(ats, "--ipv4", "--http2", *common_tls, f"https://localhost:{ats.https_port}")
        self.curl(ats, "--ipv4", "--http1.1", *common_tls, f"https://localhost:{ats.https_port}")
        self.curl(
            ats,
            "--ipv6",
            "--http1.1",
            "--proxy",
            f"localhost:{ats.ipv6_port}",
            "http://www.no-oride.com",
        )
        self.curl(ats, "--ipv6", "--http1.1", *common_tls, f"https://localhost:{ats.ipv6_https_port}")

    def run(self) -> None:
        """Start both proxies, run requests in observer order, and compare the log."""

        self._origin.start()
        self._remap_ats.start()
        self.run_remap_requests()
        self._global_ats.start()
        self.run_global_requests()
        forwarded_log = self._origin.run_directory / "forwarded.log"
        wait_for_file_lines(forwarded_log, "^-\\s*$", 20)
        assert_matches_gold(forwarded_log.read_text(errors="replace"), self._services.resolve_path("forwarded.gold"))


def test_forwarded(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Forwarded values describe the incoming protocol and client correctly."""

    curl = Curl(ats_factory.run_directory)
    if curl.uses_uds:
        pytest.skip("Forwarded client-address checks require IP listeners")
    if not curl.supports("http2") or not curl.supports("IPv6"):
        pytest.skip("curl with HTTP/2 and IPv6 support is required")
    ForwardedScenario(ats_factory, services).run()
