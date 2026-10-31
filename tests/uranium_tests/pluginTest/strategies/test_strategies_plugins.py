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

import re
import shlex

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory

STRATEGY_PLUGINS_ORIGIN_SUFFIXES = ("0", "1", "2", "p", "s")

# (tag, plugin, plugin parameter) for each remap plugin under test.
PLUGINS = (
    ("hr", "header_rewrite.so", "hdr_rw.config"),
    ("rr", "regex_remap.so", "regex_remap.config"),
    ("lua", "tslua.so", "strategies.lua"),
)

# (host prefix, remap rule strategy) for each mapping of every plugin.
MAPPINGS = (
    ("nhp", None),
    ("nhs", "nh0"),
    ("nh0", "nh0"),
    ("nh1", "nh1"),
    ("nh2", None),
)

# The 'nemo' cases must fail to resolve, and nothing else may log an ERROR.
EXPECTED_ERRORS = (
    r"\[header_rewrite\] Failed to resolve strategy 'nemo' while loading the remap rule",
    r"\[regex_remap\] Unable to resolve strategy: 'nemo'",
    r"\[ts_lua\]\[ts_lua_http_set_next_hop_strategy\] Failed get next hop strategy name 'nemo'",
)


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Create DNS records for every named next hop.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({f"nh{suffix}": ["127.0.0.1"] for suffix in STRATEGY_PLUGINS_ORIGIN_SUFFIXES})
    return dns


def configure_origins(services: ServiceFactory) -> dict[str, OriginServer]:
    """Create five origins that identify themselves in their bodies.

    :param services: Factory owning support services and their cleanup.
    """

    origins = {}
    for suffix in STRATEGY_PLUGINS_ORIGIN_SUFFIXES:
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


def global_header_rewrite_config() -> str:
    """Return global strategy mutations, including a per-transaction expanded name."""

    return "\n".join(
        (
            "cond %{READ_REQUEST_HDR_HOOK} [AND]",
            'cond %{CLIENT-HEADER:GlobalStrategy} ="nh1"',
            "set-next-hop-strategy nh1",
            "cond %{READ_REQUEST_HDR_HOOK} [AND]",
            "cond %{CLIENT-HEADER:DynStrategy} /nh/",
            "set-next-hop-strategy %{CLIENT-HEADER:DynStrategy}",
        )) + "\n"


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


def strategies_config(*, _origins: dict[str, OriginServer]) -> str:
    """Render a one-host consistent-hash strategy for each origin.

    :param _origins: Test-local origins configured by the test.
    """

    lines = ["groups:"]
    for suffix in STRATEGY_PLUGINS_ORIGIN_SUFFIXES:
        origin = _origins[f"nh{suffix}"]
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
    for suffix in STRATEGY_PLUGINS_ORIGIN_SUFFIXES:
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


def remap_config_lines() -> list[str]:
    """Return the remap.config rules for every plugin and mapping."""

    lines = []
    for tag, plugin, pparam in PLUGINS:
        for host, strategy in MAPPINGS:
            strategy_arg = f" @strategy={strategy}" if strategy else ""
            lines.append(f"map http://{host}_{tag} http://origin{strategy_arg} @plugin={plugin} @pparam={pparam}")
    return lines


def remap_yaml_lines() -> list[str]:
    """Return the remap.yaml rules equivalent to :func:`remap_config_lines`."""

    lines = ["remap:"]
    for tag, plugin, pparam in PLUGINS:
        for host, strategy in MAPPINGS:
            lines += [
                "  - type: map",
                "    from:",
                f"      url: http://{host}_{tag}",
                "    to:",
                "      url: http://origin",
            ]
            if strategy:
                lines.append(f"    strategy: {strategy}")
            lines += [
                "    plugins:",
                f"      - name: {plugin}",
                "        params:",
                f"          - {pparam}",
            ]
    return lines


def configure_ats(ats_factory: ATSFactory, use_yaml: bool, *, _dns: DNSServer, _origins: dict[str, OriginServer]) -> ATS:
    """Configure plugins, strategies, and the parent.config fallback.

    :param _dns: Test-local dns configured by the test.
    :param _origins: Test-local origins configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param use_yaml: Load the remap rules from remap.yaml instead of remap.config.
    """

    ats = ats_factory.create("ats", enable_cache=False, disable_log_checks=True)
    required = ("header_rewrite.so", "regex_remap.so", "tslua.so")
    if not all(ats.plugin_exists(plugin) for plugin in required):
        pytest.skip("header_rewrite.so, regex_remap.so, and tslua.so are required")
    ats.records.update(
        {
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.insert_response_via_str": 1,
            "proxy.config.http.uncacheable_requests_bypass_parent": 0,
            "proxy.config.http.no_dns_just_forward_to_parent": 1,
            "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
            "proxy.config.http.parent_proxy.self_detect": 0,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "next_hop|dns|http|parent|regex_remap|header_rewrite|tslua",
        })
    ats.write_config_file("hdr_rw.config", header_rewrite_config())
    ats.write_config_file("global_hdr_rw.config", global_header_rewrite_config())
    ats.plugin_config.add_line("header_rewrite.so global_hdr_rw.config")
    ats.write_config_file("regex_remap.config", regex_remap_config())
    ats.write_config_file("strategies.lua", lua_config())
    ats.write_config_file("strategies.yaml", strategies_config(_origins=_origins))
    ats.parent_config.add_line(
        f'dest_domain=. parent="nh2:{_origins["nh2"].port}" '
        "round_robin=false go_direct=false parent_is_proxy=false")
    if use_yaml:
        ats.remap_yaml.add_lines(remap_yaml_lines())
    else:
        ats.remap_config.add_lines(remap_config_lines())
    return ats


def request(
        host: str,
        path: str,
        expected_origin: str,
        *,
        strategy: str | None = None,
        header: str = "Strategy",
        _ats: ATS,
        _curl: Curl) -> None:
    """Issue one request and verify which origin served it.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    :param path: Resource or file path used by this operation.
    :param expected_origin: Expected origin for this case.
    :param strategy: Strategy used by this test step.
    :param header: Request header that carries ``strategy``.
    """

    arguments = ["--silent", "--show-error", "--proxy", f"http://127.0.0.1:{_ats.http_port}"]
    if strategy is not None:
        arguments.extend(("--header", f"{header}: {strategy}"))
    arguments.append(f"http://{host}{path}")
    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == 0, result.output
    assert result.stdout == expected_origin, result.output


def run_header_rewrite_cases(*, _ats: ATS, _curl: Curl) -> None:
    """Verify header_rewrite sets, clears, and rejects strategy names.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

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
        request(host, "/path", expected, strategy=strategy, _ats=_ats, _curl=_curl)


def run_global_header_rewrite_cases(*, _ats: ATS, _curl: Curl) -> None:
    """Verify a global header_rewrite resolves strategies per transaction.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    request("nhp_hr", "/path", "nh1", strategy="nh1", header="GlobalStrategy", _ats=_ats, _curl=_curl)
    request("nhp_hr", "/path", "nh0", strategy="nh0", header="DynStrategy", _ats=_ats, _curl=_curl)


def check_diags(_ats: ATS) -> None:
    """Verify each expected strategy failure was logged and no other ERROR was.

    :param _ats: Test-local ats configured by the test.
    """

    content = _ats.diags_log.read_text(errors="replace")
    for pattern in EXPECTED_ERRORS:
        assert re.search(pattern, content), f"missing expected error {pattern!r}:\n{content}"
    unexpected = re.findall(rf"ERROR: (?!{'|'.join(EXPECTED_ERRORS)}).*", content)
    assert not unexpected, f"unexpected errors: {unexpected}"


def run_plugin_cases(suffix: str, *, _ats: ATS, _curl: Curl) -> None:
    """Verify regex_remap or tslua changes strategy from the request path.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param suffix: Suffix used by this test step.
    """

    for host, path, expected in (
        (f"nhp_{suffix}", "/nhp" if suffix == "rr" else "/nh", "nh2"),
        (f"nhs_{suffix}", "/nh", "nh0"),
        (f"nh0_{suffix}", "/nh0", "nh1"),
        (f"nh1_{suffix}", "/nh1", "nh2"),
        (f"nh2_{suffix}", "/nh2", "nh0"),
        (f"nh0_{suffix}", "/nemo", "nh0"),
    ):
        request(host, path, expected, _ats=_ats, _curl=_curl)


@pytest.mark.parametrize("use_yaml", (False, True), ids=("remap_config", "remap_yaml"))
def test_strategies_plugins(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, use_yaml: bool) -> None:
    """Remap plugins can move among strategies and the parent.config fallback.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    :param use_yaml: Load the remap rules from remap.yaml instead of remap.config.
    """
    _dns = configure_dns(services)
    _origins = configure_origins(services)
    _ats = configure_ats(ats_factory, use_yaml, _dns=_dns, _origins=_origins)

    _dns.start()
    for origin in _origins.values():
        origin.start()
    _ats.start()
    run_header_rewrite_cases(_ats=_ats, _curl=curl)
    run_plugin_cases("rr", _ats=_ats, _curl=curl)
    run_plugin_cases("lua", _ats=_ats, _curl=curl)
    run_global_header_rewrite_cases(_ats=_ats, _curl=curl)
    _ats.stop()
    check_diags(_ats)
