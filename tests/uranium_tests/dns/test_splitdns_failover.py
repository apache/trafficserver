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
import time

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory


def configure_dns(services: ServiceFactory) -> tuple[DNSServer, DNSServer]:
    """Create the dead primary and live secondary SplitDNS nameservers."""
    records = {f"{host}.ts.a.o.": ["127.0.0.1"] for host in ("a", "b", "c", "d")}
    primary = services.dns("dead-primary")
    primary.add_records(records)
    secondary = services.dns("live-secondary")
    secondary.add_records(records)
    return primary, secondary


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the origin response used by all four lookups."""
    origin = services.origin("origin")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, primary: DNSServer, secondary: DNSServer, origin: OriginServer) -> ATS:
    """Configure round-robin SplitDNS with a dead primary and live secondary."""
    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update(
        {
            "proxy.config.dns.splitDNS.enabled": 1,
            "proxy.config.dns.round_robin_nameservers": 1,
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "dns|splitdns",
        })
    ats.splitdns_config.add_line(f"dest_domain=ts.a.o named=\"127.0.0.1:{primary.port} 127.0.0.1:{secondary.port}\"")
    for host in ("a", "b", "c", "d"):
        ats.remap_config.add_line(f"map /{host}/ http://{host}.ts.a.o:{origin.port}/")
    return ats


def request(host: str, *, ats: ATS, curl: Curl) -> None:
    """Resolve one host through SplitDNS and verify the origin response."""
    result = curl.get(ats, f"/{host}/", options="--verbose")
    assert result.returncode == 0, result.output
    assert "HTTP/1.1 200 OK" in result.output


def test_splitdns_failover(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Round-robin SplitDNS fails over and later retries the recovered primary."""
    primary, secondary = configure_dns(services)
    origin = configure_origin(services)
    ats = configure_ats(ats_factory, primary, secondary, origin)
    secondary.start()
    origin.start()
    ats.start()
    request("a", ats=ats, curl=curl)
    request("b", ats=ats, curl=curl)
    request("c", ats=ats, curl=curl)
    primary.start()
    time.sleep(11)
    request("d", ats=ats, curl=curl)
    diagnostics = ats.diags_log.read_text(errors="replace")
    traffic = ats.traffic_out.read_text(errors="replace")
    assert re.search(r"connection to DNS server .* lost, marking as down", diagnostics)
    assert re.search(r"round-robin: nameserver 0 DNS response code = 0", traffic)
