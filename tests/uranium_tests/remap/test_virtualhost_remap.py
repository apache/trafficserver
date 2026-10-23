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

import copy

import yaml

from tools.uranium.services import ATS, Curl, ServiceFactory, wait_for_file_lines

from ..jsonrpc.config_reload_helpers import wait_for_status


def virtualhost_entry(identifier: str, domains: list[str], routes: list[tuple[str, str]], origin: str) -> dict[str, object]:
    """Create one virtualhost with identifiable remap targets.

    :param identifier: Reloadable virtualhost identifier.
    :param domains: Exact or wildcard domain selectors.
    :param routes: Source URLs and origin paths.
    :param origin: Backend host and port.
    """
    return {
        "id": identifier,
        "domains": domains,
        "remap":
            [{
                "type": "map",
                "from": {
                    "url": source
                },
                "to": {
                    "url": f"http://{origin}/{target}/"
                }
            } for source, target in routes]
    }


def test_virtualhost_remap(ats: ATS, services: ServiceFactory, curl: Curl) -> None:
    """Select exact/wildcard tables and preserve both entries after a refused reload.

    :param ats: Proxy with both global and per-domain remap tables.
    :param services: Factory owning the path-keyed backend.
    :param curl: Transport-aware request client.
    """
    server = services.origin("server")
    for path in ("vhost-exact-domain", "vhost-deep-wildcard", "vhost-wide-wildcard", "vhost-wildcard-precedence",
                 "vhost-exact-precedence", "vhost-fallback-rule", "global-fallback/other", "global-plain", "vhost-conflict"):
        body = "hit:" + ("global-fallback" if path == "global-fallback/other" else path)
        server.add_response(
            {
                "headers": f"GET /{path}/ HTTP/1.1\r\nHost: origin.example.com\r\n\r\n",
                "body": ""
            }, {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": body
            })
    origin = f"127.0.0.1:{server.port}"
    ats.records.update({"proxy.config.diags.debug.enabled": 1, "proxy.config.diags.debug.tags": "virtualhost|url_rewrite"})
    ats.remap_config.add_lines(
        [
            f"map http://fallback.example.net/ http://{origin}/global-fallback/",
            f"map http://none.example.net/ http://{origin}/global-plain/"
        ])
    entries = [
        virtualhost_entry("exact-only", ["exact.example.org"], [("http://exact.example.org/", "vhost-exact-domain")], origin),
        virtualhost_entry("deep-wildcard", ["*.deep.example.com"], [("http://x.deep.example.com/", "vhost-deep-wildcard")], origin),
        virtualhost_entry(
            "wide-wildcard", ["*.example.com"], [
                ("http://x.deep.example.com/", "vhost-wide-wildcard"),
                ("http://precedence.example.com/", "vhost-wildcard-precedence")
            ], origin),
        virtualhost_entry(
            "exact-precedence", ["precedence.example.com"], [("http://precedence.example.com/", "vhost-exact-precedence")], origin),
        virtualhost_entry(
            "path-miss", ["fallback.example.net"], [("http://fallback.example.net/only-here/", "vhost-fallback-rule")], origin),
    ]
    ats.write_config_file("virtualhost.yaml", yaml.safe_dump({"virtualhost": entries}))
    server.start()
    ats.start()
    cases = [
        ("exact.example.org", "/", "vhost-exact-domain", "global-plain"),
        ("x.deep.example.com", "/", "vhost-deep-wildcard", "vhost-wide-wildcard"),
        ("precedence.example.com", "/", "vhost-exact-precedence", "vhost-wildcard-precedence"),
        ("fallback.example.net", "/other/", "global-fallback", "vhost-fallback-rule"),
        ("none.example.net", "/", "global-plain", None),
    ]
    for host, path, expected, forbidden in cases:
        result = curl.get(ats, path, headers={"Host": host})
        assert result.returncode == 0, result.output
        assert f"hit:{expected}" in result.stdout, result.output
        if forbidden:
            assert f"hit:{forbidden}" not in result.stdout, result.output
    conflict = copy.deepcopy(entries[:2])
    conflict[1] = virtualhost_entry(
        "deep-wildcard", ["*.deep.example.com", "exact.example.org"],
        [("http://x.deep.example.com/", "vhost-conflict"), ("http://exact.example.org/", "vhost-conflict")], origin)
    (ats.config_directory / "virtualhost.yaml").write_text(yaml.safe_dump({"virtualhost": conflict}))
    result = ats.traffic_ctl("config", "reload", "-t", "vhost-conflict", "--directive=virtualhost.id=deep-wildcard")
    assert result.returncode == 0, result.output
    wait_for_status(ats, "vhost-conflict", "virtualhost", "fail")
    wait_for_file_lines(ats.diags_log, "is already claimed by virtualhost 'exact-only'", 1)
    for host, expected in (("x.deep.example.com", "vhost-deep-wildcard"), ("exact.example.org", "vhost-exact-domain")):
        result = curl.get(ats, headers={"Host": host})
        assert result.returncode == 0, result.output
        assert f"hit:{expected}" in result.stdout, result.output
        assert "hit:vhost-conflict" not in result.stdout
