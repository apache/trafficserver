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

import re
import shlex
import urllib.parse

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
    wait_for_metric,
)


def test_prefetch_cmcd(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """CMCD nor prefetches relative URLs and ignores requests containing nrr.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """

    def add_origin_response(path: str, body_name: str, cmcd: str | None = None) -> None:
        """Add one cacheable origin resource.

        :param path: Resource or file path used by this operation.
        :param body_name: Body name used by this test step.
        :param cmcd: Cmcd used by this test step.
        """

        cmcd_line = "" if cmcd is None else f"Cmcd-Request: {cmcd}\r\n"
        _origin.add_response(
            {"headers": f"GET {path} HTTP/1.1\r\nHost: does.not.matter\r\n{cmcd_line}\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=60\r\n\r\n",
                "body": f"This is the body for {body_name}\n",
            },
        )

    def configure_server() -> OriginServer:
        """Create requested and prefetched CMCD resources."""
        nonlocal _origin

        origin = services.origin("origin")
        _origin = origin
        add_origin_response("/tests/request.txt", "request.txt", 'foo=12,nor="prefetch.txt",bar=42')
        query_target = "query?bar=baz"
        encoded = urllib.parse.quote(query_target)
        add_origin_response("/tests/query?this=foo&that", "query?this=foo&that", f'nor="{encoded}"')
        add_origin_response("/tests/prefetch.txt", "prefetch.txt")
        add_origin_response("/tests/query?bar=baz", query_target)
        add_origin_response("/root.txt", "root.txt", 'nor="rooted"')
        add_origin_response("/rooted", "rooted")
        add_origin_response("/tests/crr.txt", "crr.txt", 'foo=12,nor="crr.txt",bar=42,nrr="0-"')
        return origin

    def configure_dns() -> DNSServer:
        """Resolve both proxy hostnames locally."""

        dns = services.dns("dns")
        dns.add_records({"ts0": ["127.0.0.1"], "ts1": ["127.0.0.1"]})
        return dns

    def common_records() -> dict[str, object]:
        """Return records shared by the front and next-hop caches."""

        return {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "prefetch|http",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.parent_proxy.self_detect": 0,
            "proxy.config.log.max_secs_per_buffer": 1,
        }

    def configure_logging(ats: ATS) -> None:
        """Log request, status, cache result, write result, length, and prefetch source.

        :param ats: Traffic Server instance configured or queried by this step.
        """

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

    def require_plugins(ats: ATS) -> None:
        """Skip when either remap plugin is unavailable.

        :param ats: Traffic Server instance configured or queried by this step.
        """

        if not ats.plugin_exists("prefetch.so") or not ats.plugin_exists("cachekey.so"):
            pytest.skip("prefetch.so and cachekey.so are required")

    def configure_next_hop(ats_factory: ATSFactory) -> ATS:
        """Configure the back cache to accept internal prefetch requests.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts1")
        require_plugins(ats)
        ats.records.update(common_records())
        ats.remap_config.add_line(
            f"map / http://127.0.0.1:{_origin.port} "
            "@plugin=cachekey.so @pparam==--sort-params=true @plugin=prefetch.so @pparam==--front=false")
        configure_logging(ats)
        return ats

    def configure_front(ats_factory: ATSFactory) -> ATS:
        """Configure the front cache to parse CMCD nor fields.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts0")
        require_plugins(ats)
        ats.records.update(common_records())
        ats.remap_config.add_line(
            f"map http://ts0 http://ts1:{_next_hop.http_port} "
            "@plugin=cachekey.so @pparam=--sort-params=true @plugin=prefetch.so "
            "@pparam=--front=true @pparam=--fetch-policy=simple @pparam=--cmcd-nor=true")
        configure_logging(ats)
        return ats

    def request(path: str, cmcd: str | None = None) -> None:
        """Issue one request through the front cache.

        :param path: Resource or file path used by this operation.
        :param cmcd: Cmcd used by this test step.
        """

        arguments = ["--silent", "--show-error", "--proxy", f"127.0.0.1:{_front.http_port}"]
        if cmcd is not None:
            arguments.extend(("--header", f"Cmcd-Request: {cmcd}"))
        arguments.append(f"http://ts0{path}")
        result = _curl.run_for(
            _front,
            shlex.join(arguments),
        )
        assert result.returncode == 0, result.output

    def wait_for_next_hop_cache_fill(path: str) -> None:
        """Wait for a next-hop transaction to finish writing its cache entry.

        :param path: Request path expected in the next-hop transaction log.
        """

        transaction_log = _next_hop.log_directory / "transaction.log"
        wait_for_file_lines(transaction_log, rf"^{re.escape(path)} 200 TCP_MISS FIN ", 1, timeout=30)
        wait_for_metric(_next_hop, "proxy.process.cache.write.active", 0, timeout=30)

    _origin = configure_server()
    _dns = configure_dns()
    _next_hop = configure_next_hop(ats_factory)
    _front = configure_front(ats_factory)
    _curl = Curl(ats_factory.run_directory)

    _origin.start()
    _dns.start()
    _next_hop.start()
    _front.start()
    request_cmcd = 'foo=12,nor="prefetch.txt",bar=42'
    request("/tests/request.txt")
    wait_for_next_hop_cache_fill("/tests/request.txt")
    request("/tests/request.txt", request_cmcd)
    wait_for_next_hop_cache_fill("/tests/prefetch.txt")
    request("/tests/prefetch.txt")
    request("/tests/request.txt", request_cmcd)
    request("/tests/prefetch.txt")
    query_cmcd = f'nor="{urllib.parse.quote("query?bar=baz")}"'
    request("/tests/query?this=foo&that", query_cmcd)
    wait_for_next_hop_cache_fill("/tests/query?bar=baz")
    request("/tests/query?bar=baz")
    request("/root.txt", 'nor="rooted"')
    wait_for_next_hop_cache_fill("/rooted")
    request("/crr.txt", 'foo=12,nor="crr.txt",bar=42,nrr="0-"')

    front_log = _front.log_directory / "transaction.log"
    next_log = _next_hop.log_directory / "transaction.log"
    front = wait_for_file_lines(front_log, "crr.txt", 1, timeout=15)
    next_hop = wait_for_file_lines(next_log, "crr.txt", 1, timeout=15)
    assert_matches_gold(front, services.resolve_path("prefetch_cmcd0.gold"))
    assert_matches_gold(next_hop, services.resolve_path("prefetch_cmcd1.gold"))
