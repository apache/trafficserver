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
"""Consistent-hash parent selection, optional schemes, and peer rings."""
from pathlib import Path
import re
import pytest
import yaml
from tools.uranium.services import ATSFactory, Curl, ServiceFactory, assert_matches_gold


@pytest.mark.parametrize("mode", ("basic", "optional-scheme", "peer", "peer-direct"))
def test_parent_select(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl, mode: str) -> None:
    """Preserve distribution and peer-selection traces across all topologies.

    :param ats_factory: Factory for independently cached proxies.
    :param services: Origin and DNS factories.
    :param curl: Independent request runner.
    :param mode: Parent-only, scheme-optional, or one of the two peer topologies.
    """
    directory = Path(__file__).parent
    peer_mode = mode.startswith("peer")
    direct = mode == "peer-direct"
    origin = services.origin("origin")
    count = 16 if peer_mode else 32
    for i in range(count):
        origin.add_response(
            {"headers": f"GET /obj{i} HTTP/1.1\r\nHost: does.not.matter\r\n\r\n"}, {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
                "body": "This is the body.\n"
            })
    dns = services.dns("dns")
    upstream = [ats_factory.create(f"ts_upstream{i}" if peer_mode else f"ts_nh{i}") for i in range(6 if peer_mode else 8)]
    peers = [ats_factory.create(f"ts_peer{i}") for i in range(8)] if peer_mode else [ats_factory.create("ts")]
    if not peers[0].plugin_exists("parent_select.so"):
        pytest.skip("parent_select.so is not installed")
    for proxy in upstream + peers:
        proxy.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|dns|parent|next_hop|host_statuses|hostdb|cachekey|pparent_select",
                "proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}",
                "proxy.config.dns.resolv_conf": "NULL",
            })
    for i, proxy in enumerate(upstream):
        dns.add_records({f"ts_upstream{i}" if peer_mode else f"next_hop{i}": ["127.0.0.1"]})
        proxy.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}")
    for i in range(len(peers)):
        dns.add_records({f"ts_peer{i}": ["127.0.0.1"]})
    for i, proxy in enumerate(peers):
        proxy.records.update(
            {
                "proxy.config.http.cache.http": int(peer_mode),
                "proxy.config.http.cache.required_headers": 0,
                "proxy.config.http.uncacheable_requests_bypass_parent": 0,
                "proxy.config.http.no_dns_just_forward_to_parent": 0 if direct else 1,
                "proxy.config.http.parent_proxy.mark_down_hostdb": 0,
                "proxy.config.http.parent_proxy.self_detect": int(peer_mode),
            })
        targets = peers if peer_mode else upstream
        group = []
        for j, target in enumerate(targets):
            protocol = {"port": target.http_port}
            if mode != "optional-scheme":
                protocol["scheme"] = "http"
            group.append({"host": f"ts_peer{j}" if peer_mode else f"next_hop{j}", "protocol": [protocol], "weight": 1.0})
        groups = [group]
        if peer_mode and not direct:
            groups.append(
                [
                    {
                        "host": f"ts_upstream{j}",
                        "protocol": [{
                            "scheme": "http",
                            "port": target.http_port
                        }],
                        "weight": 1.0
                    } for j, target in enumerate(upstream)
                ])
        strategy = {
            "strategy": "the-strategy",
            "policy": "consistent_hash",
            "hash_key": "cache_key" if mode == "peer" else "path",
            "go_direct": direct,
            "parent_is_proxy": True,
            "ignore_self_detect": not peer_mode,
            "groups": groups,
        }
        if mode != "optional-scheme":
            strategy["scheme"] = "http"
        if peer_mode:
            strategy.update({"cache_peer_result": False, "failover": {"ring_mode": "peering_ring", "self": f"ts_peer{i}"}})
        proxy.write_config_file("strategies.yaml", yaml.safe_dump({"strategies": [strategy]}))
        suffix = f" @plugin=parent_select.so @pparam={proxy.config_directory}/strategies.yaml @pparam=the-strategy"
        if direct:
            for j, target in enumerate(upstream):
                url = f"http://ts_upstream{j}:{target.http_port}/"
                proxy.remap_config.add_line(f"map {url} {url}{suffix}")
        elif peer_mode:
            suffix += " @plugin=cachekey.so @pparam=--uri-type=remap @pparam=--capture-prefix=/(.*):(.*)/$1/"
            proxy.remap_config.add_lines(
                ["map http://dummy.com http://not_used" + suffix, "map http://not_used http://also_not_used" + suffix])
        else:
            proxy.remap_config.add_line("map http://dummy.com http://not_used" + suffix)
    origin.start()
    dns.start()
    for proxy in upstream + peers:
        proxy.start()
    for multiplier in ((1, 3) if peer_mode else (1,)):
        for i in range(count):
            proxy = peers[(i * multiplier) % len(peers)]
            host = f"ts_upstream0:{upstream[0].http_port}" if direct else "dummy.com"
            result = curl.run_for(proxy, f"--verbose --proxy 127.0.0.1:{proxy.http_port} http://{host}/obj{i}")
            assert result.returncode == 0, result.output
            assert_matches_gold(
                result.stdout, directory / ("peer2.body.gold" if direct else "peer.body.gold" if peer_mode else "body.gold"))
    for proxy in peers + upstream:
        proxy.stop()
    output = ""
    for i, proxy in enumerate(peers if peer_mode else upstream):
        for line in proxy.traffic_out.read_text(errors="replace").splitlines():
            if not peer_mode:
                if "200 OK" in line:
                    output += f"nh_trace{i}.log:{line}\n"
            elif re.search(r"^\+\+\+|^[A-Z].*TTP/|^.alts. --|PARENT_SPECIFIED", line):
                line = re.sub(r"^.*\(pparent_select\) [^ ]* ", "", f"trace_peer{i}.log:{line}")
                if "PARENT_SPECIFIED" in line:
                    line = re.sub(r"\.[0-9]*$", "", line)
                if direct:
                    for j, target in enumerate(upstream):
                        line = line.replace(f":{target.http_port}", f":UP_PORT{j}")
                output += line + "\n"
    assert_matches_gold(output, directory / ("peer2.trace.gold" if direct else "peer.trace.gold" if peer_mode else "trace.gold"))
