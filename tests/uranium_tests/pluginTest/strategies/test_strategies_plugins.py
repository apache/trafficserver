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
"""Verify remap plugins can select and clear next-hop strategies."""

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory


class StrategyPluginsScenario:
    """Exercise header_rewrite, regex_remap, and tslua strategy selection."""

    ORIGIN_SUFFIXES = ("0", "1", "2", "p", "s")

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        self._dns = self.configure_dns(services)
        self._origins = self.configure_origins(services)
        self._ats = self.configure_ats(ats_factory)
        self._curl = curl

    @staticmethod
    def configure_dns(services: ServiceFactory) -> DNSServer:
        """Create DNS records for every named next hop."""

        dns = services.dns("dns")
        dns.add_records({f"nh{suffix}": ["127.0.0.1"] for suffix in StrategyPluginsScenario.ORIGIN_SUFFIXES})
        return dns

    @classmethod
    def configure_origins(cls, services: ServiceFactory) -> dict[str, OriginServer]:
        """Create five origins that identify themselves in their bodies."""

        origins = {}
        for suffix in cls.ORIGIN_SUFFIXES:
            name = f"nh{suffix}"
            origin = services.origin(name)
            for path in ("/path", f"/path/{name}"):
                origin.add_response(
                    {"headers": f"GET {path} HTTP/1.1\r\nHost: origin\r\n\r\n"},
                    {
                        "headers": f"HTTP/1.1 200 OK\r\nConnection: close\r\nOrigin: {name}\r\n\r\n",
                        "body": name,
                    },
                )
            origins[name] = origin
        return origins

    @staticmethod
    def header_rewrite_config() -> str:
        """Return strategy mutations selected by a request header."""

        return "\n".join(
            (
                'cond %{CLIENT-HEADER:Strategy} ="nemo"',
                "set-next-hop-strategy nemo",
                'cond %{CLIENT-HEADER:Strategy} ="nh0"',
                "set-next-hop-strategy nh0",
                'cond %{CLIENT-HEADER:Strategy} ="nh1"',
                "set-next-hop-strategy nh1",
                'cond %{CLIENT-HEADER:Strategy} ="null"',
                "set-next-hop-strategy null",
                'cond %{CLIENT-HEADER:Strategy} ="clear"',
                'set-next-hop-strategy ""',
            )) + "\n"

    @staticmethod
    def regex_remap_config() -> str:
        """Return path-selected strategy mutations for regex_remap."""

        return "\n".join(
            (
                "/nh0 http://origin/path @strategy=nh1",
                "/nh1 http://origin/path @strategy=",
                "/nh2 http://origin/path @strategy=nh0",
                "/null http://origin/path @strategy=null",
                "/nemo http://origin/path @strategy=nemo",
                "/ http://origin/path",
            )) + "\n"

    @staticmethod
    def lua_config() -> str:
        """Return URI-selected strategy mutations for tslua."""

        return "\n".join(
            (
                "function do_remap()",
                " local uri = ts.client_request.get_uri()",
                ' if uri:find("nh0") then',
                '  ts.http.set_next_hop_strategy("nh1")',
                ' elseif uri:find("nh1") then',
                '  ts.http.set_next_hop_strategy("")',
                ' elseif uri:find("nh2") then',
                '  ts.http.set_next_hop_strategy("nh0")',
                ' elseif uri:find("null") then',
                '  ts.http.set_next_hop_strategy("null")',
                ' elseif uri:find("nemo") then',
                '  ts.http.set_next_hop_strategy("nemo")',
                " end",
                ' ts.client_request.set_uri("path")',
                " return 0",
                "end",
            )) + "\n"

    def strategies_config(self) -> str:
        """Render a one-host consistent-hash strategy for each origin."""

        lines = ["groups:"]
        for suffix in self.ORIGIN_SUFFIXES:
            origin = self._origins[f"nh{suffix}"]
            lines.extend(
                (
                    f"  - &g{suffix}",
                    f"    - host: nh{suffix}",
                    "      protocol:",
                    "        - scheme: http",
                    f"          port: {origin.port}",
                    "      weight: 1.0",
                ))
        lines.append("strategies:")
        for suffix in self.ORIGIN_SUFFIXES:
            lines.extend(
                (
                    f"  - strategy: nh{suffix}",
                    "    policy: consistent_hash",
                    "    hash_key: path",
                    "    go_direct: false",
                    "    parent_is_proxy: false",
                    "    ignore_self_detect: true",
                    "    groups:",
                    f"      - *g{suffix}",
                    "    scheme: http",
                ))
        return "\n".join(lines) + "\n"

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure plugins, strategies, and the parent.config fallback."""

        ats = ats_factory.create("ats", enable_cache=False)
        required = ("header_rewrite.so", "regex_remap.so", "tslua.so")
        if not all(ats.plugin_exists(plugin) for plugin in required):
            pytest.skip("header_rewrite.so, regex_remap.so, and tslua.so are required")
        ats.records.update(
            {
                "proxy.config.dns.nameservers": f"127.0.0.1:{self._dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
                "proxy.config.http.insert_response_via_str": 1,
                "proxy.config.http.uncacheable_requests_bypass_parent": 0,
                "proxy.config.http.no_dns_just_forward_to_parent": 1,
                "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
                "proxy.config.http.parent_proxy.self_detect": 0,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "next_hop|dns|http|parent|regex_remap|header_rewrite|tslua",
            })
        ats.write_config_file("hdr_rw.config", self.header_rewrite_config())
        ats.write_config_file("regex_remap.config", self.regex_remap_config())
        ats.write_config_file("strategies.lua", self.lua_config())
        ats.write_config_file("strategies.yaml", self.strategies_config())
        ats.parent_config.add_line(
            f'dest_domain=. parent="nh2:{self._origins["nh2"].port}" '
            "round_robin=false go_direct=false parent_is_proxy=false")
        ats.remap_config.add_lines(
            (
                "map http://nhp_hr http://origin @plugin=header_rewrite.so @pparam=hdr_rw.config",
                "map http://nhs_hr http://origin @strategy=nh0 @plugin=header_rewrite.so @pparam=hdr_rw.config",
                "map http://nh0_hr http://origin @strategy=nh0 @plugin=header_rewrite.so @pparam=hdr_rw.config",
                "map http://nh1_hr http://origin @strategy=nh1 @plugin=header_rewrite.so @pparam=hdr_rw.config",
                "map http://nh2_hr http://origin @plugin=header_rewrite.so @pparam=hdr_rw.config",
                "map http://nhp_rr http://origin @plugin=regex_remap.so @pparam=regex_remap.config",
                "map http://nhs_rr http://origin @strategy=nh0 @plugin=regex_remap.so @pparam=regex_remap.config",
                "map http://nh0_rr http://origin @strategy=nh0 @plugin=regex_remap.so @pparam=regex_remap.config",
                "map http://nh1_rr http://origin @strategy=nh1 @plugin=regex_remap.so @pparam=regex_remap.config",
                "map http://nh2_rr http://origin @plugin=regex_remap.so @pparam=regex_remap.config",
                "map http://nhp_lua http://origin @plugin=tslua.so @pparam=strategies.lua",
                "map http://nhs_lua http://origin @strategy=nh0 @plugin=tslua.so @pparam=strategies.lua",
                "map http://nh0_lua http://origin @strategy=nh0 @plugin=tslua.so @pparam=strategies.lua",
                "map http://nh1_lua http://origin @strategy=nh1 @plugin=tslua.so @pparam=strategies.lua",
                "map http://nh2_lua http://origin @plugin=tslua.so @pparam=strategies.lua",
            ))
        return ats

    def request(self, host: str, path: str, expected_origin: str, *, strategy: str | None = None) -> None:
        """Issue one request and verify which origin served it."""

        arguments = ["--silent", "--show-error", "--proxy", f"http://127.0.0.1:{self._ats.http_port}"]
        if strategy is not None:
            arguments.extend(("--header", f"Strategy: {strategy}"))
        arguments.append(f"http://{host}{path}")
        result = self._curl.run_for(self._ats, *arguments)
        assert result.returncode == 0, result.output
        assert result.stdout == expected_origin, result.output

    def run_header_rewrite_cases(self) -> None:
        """Verify header_rewrite sets, clears, and rejects strategy names."""

        for host, strategy, expected in (
            ("nhp_hr", None, "nh2"),
            ("nhs_hr", None, "nh0"),
            ("nh0_hr", None, "nh0"),
            ("nh1_hr", None, "nh1"),
            ("nh2_hr", None, "nh2"),
            ("nh0_hr", "nh1", "nh1"),
            ("nh1_hr", "null", "nh2"),
            ("nh2_hr", "nh0", "nh0"),
            ("nh0_hr", "nemo", "nh0"),
        ):
            self.request(host, "/path", expected, strategy=strategy)

    def run_plugin_cases(self, suffix: str) -> None:
        """Verify regex_remap or tslua changes strategy from the request path."""

        for host, path, expected in (
            (f"nhp_{suffix}", "/nhp" if suffix == "rr" else "/nh", "nh2"),
            (f"nhs_{suffix}", "/nh", "nh0"),
            (f"nh0_{suffix}", "/nh0", "nh1"),
            (f"nh1_{suffix}", "/nh1", "nh2"),
            (f"nh2_{suffix}", "/nh2", "nh0"),
            (f"nh0_{suffix}", "/nemo", "nh0"),
        ):
            self.request(host, path, expected)

    def run(self) -> None:
        """Start all dependencies and run the 21 strategy selections."""

        self._dns.start()
        for origin in self._origins.values():
            origin.start()
        self._ats.start()
        self.run_header_rewrite_cases()
        self.run_plugin_cases("rr")
        self.run_plugin_cases("lua")
        assert "ERROR" in self._ats.diags_log.read_text(errors="replace")


def test_strategies_plugins(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Remap plugins can move among strategies and the parent.config fallback."""

    StrategyPluginsScenario(ats_factory, services, curl).run()
