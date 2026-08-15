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
"""Verify CMCD next-object prefetch across a two-tier ATS topology."""

import urllib.parse
import time

import pytest

from tools.uranium.services import (
    ATS,
    ATSFactory,
    Curl,
    DNSServer,
    OriginServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)


class PrefetchCmcdScenario:
    """Configure front and next-hop caches for CMCD simple prefetch."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory) -> None:
        self._services = services
        self._origin = self.configure_server()
        self._dns = self.configure_dns()
        self._next_hop = self.configure_next_hop(ats_factory)
        self._front = self.configure_front(ats_factory)
        self._curl = Curl(ats_factory.run_directory)

    def add_origin_response(self, path: str, body_name: str, cmcd: str | None = None) -> None:
        """Add one cacheable origin resource."""

        cmcd_line = "" if cmcd is None else f"Cmcd-Request: {cmcd}\r\n"
        self._origin.add_response(
            {"headers": f"GET {path} HTTP/1.1\r\nHost: does.not.matter\r\n{cmcd_line}\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=60\r\n\r\n",
                "body": f"This is the body for {body_name}\n",
            },
        )

    def configure_server(self) -> OriginServer:
        """Create requested and prefetched CMCD resources."""

        origin = self._services.origin("origin")
        self._origin = origin
        self.add_origin_response("/tests/request.txt", "request.txt", 'foo=12,nor="prefetch.txt",bar=42')
        query_target = "query?bar=baz"
        encoded = urllib.parse.quote(query_target)
        self.add_origin_response("/tests/query?this=foo&that", "query?this=foo&that", f'nor="{encoded}"')
        self.add_origin_response("/tests/prefetch.txt", "prefetch.txt")
        self.add_origin_response("/tests/query?bar=baz", query_target)
        self.add_origin_response("/root.txt", "root.txt", 'nor="rooted"')
        self.add_origin_response("/rooted", "rooted")
        self.add_origin_response("/tests/crr.txt", "crr.txt", 'foo=12,nor="crr.txt",bar=42,nrr="0-"')
        return origin

    def configure_dns(self) -> DNSServer:
        """Resolve both proxy hostnames locally."""

        dns = self._services.dns("dns")
        dns.add_records({"ts0": ["127.0.0.1"], "ts1": ["127.0.0.1"]})
        return dns

    def common_records(self) -> dict[str, object]:
        """Return records shared by the front and next-hop caches."""

        return {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "prefetch|http",
            "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.parent_proxy.self_detect": 0,
            "proxy.config.log.max_secs_per_buffer": 1,
        }

    @staticmethod
    def configure_logging(ats: ATS) -> None:
        """Log request, status, cache result, write result, length, and prefetch source."""

        ats.set_logging_yaml(
            {
                "logging":
                    {
                        "formats": [{
                            "name": "custom",
                            "format": "%<cquup> %<pssc> %<crc> %<cwr> %<pscl> %<{X-CDN-Prefetch}cqh>",
                        }],
                        "logs": [{
                            "filename": "transaction",
                            "format": "custom"
                        }],
                    }
            })

    def require_plugins(self, ats: ATS) -> None:
        """Skip when either remap plugin is unavailable."""

        if not ats.plugin_exists("prefetch.so") or not ats.plugin_exists("cachekey.so"):
            pytest.skip("prefetch.so and cachekey.so are required")

    def configure_next_hop(self, ats_factory: ATSFactory) -> ATS:
        """Configure the back cache to accept internal prefetch requests."""

        ats = ats_factory.create("ts1")
        self.require_plugins(ats)
        ats.records.update(self.common_records())
        ats.remap_config.add_line(
            f"map / http://127.0.0.1:{self._origin.port} "
            "@plugin=cachekey.so @pparam==--sort-params=true @plugin=prefetch.so @pparam==--front=false")
        self.configure_logging(ats)
        return ats

    def configure_front(self, ats_factory: ATSFactory) -> ATS:
        """Configure the front cache to parse CMCD nor fields."""

        ats = ats_factory.create("ts0")
        self.require_plugins(ats)
        ats.records.update(self.common_records())
        ats.remap_config.add_line(
            f"map http://ts0 http://ts1:{self._next_hop.http_port} "
            "@plugin=cachekey.so @pparam=--sort-params=true @plugin=prefetch.so "
            "@pparam=--front=true @pparam=--fetch-policy=simple @pparam=--cmcd-nor=true")
        self.configure_logging(ats)
        return ats

    def request(self, path: str, cmcd: str | None = None) -> None:
        """Issue one request through the front cache."""

        arguments = ["--silent", "--show-error", "--proxy", f"127.0.0.1:{self._front.http_port}"]
        if cmcd is not None:
            arguments.extend(("--header", f"Cmcd-Request: {cmcd}"))
        arguments.append(f"http://ts0{path}")
        result = self._curl.run_for(self._front, *arguments)
        assert result.returncode == 0, result.output

    def validate_logs(self) -> None:
        """Wait for and compare both cache transaction logs."""

        front_log = self._front.log_directory / "transaction.log"
        next_log = self._next_hop.log_directory / "transaction.log"
        front = wait_for_file_lines(front_log, "crr.txt", 1, timeout=15)
        next_hop = wait_for_file_lines(next_log, "crr.txt", 1, timeout=15)
        assert_matches_gold(front, self._services.resolve_path("prefetch_cmcd0.gold"))
        assert_matches_gold(next_hop, self._services.resolve_path("prefetch_cmcd1.gold"))

    def run(self) -> None:
        """Exercise plain, encoded-query, root, repeat, and nrr-suppressed prefetches."""

        self._origin.start()
        self._dns.start()
        self._next_hop.start()
        self._front.start()
        request_cmcd = 'foo=12,nor="prefetch.txt",bar=42'
        self.request("/tests/request.txt")
        time.sleep(1)
        self.request("/tests/request.txt", request_cmcd)
        time.sleep(1)
        self.request("/tests/prefetch.txt")
        self.request("/tests/request.txt", request_cmcd)
        self.request("/tests/prefetch.txt")
        query_cmcd = f'nor="{urllib.parse.quote("query?bar=baz")}"'
        self.request("/tests/query?this=foo&that", query_cmcd)
        self.request("/tests/query?bar=baz")
        self.request("/root.txt", 'nor="rooted"')
        self.request("/crr.txt", 'foo=12,nor="crr.txt",bar=42,nrr="0-"')
        self.validate_logs()


def test_prefetch_cmcd(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """CMCD nor prefetches relative URLs and ignores requests containing nrr."""

    PrefetchCmcdScenario(ats_factory, services).run()
