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

from pathlib import Path

from tools.uranium.services import ATS, ATSFactory, Curl, DNSServer, OriginServer, ServiceFactory

SSL_DIRECTORY = Path(__file__).parent / "ssl"


def configure_origin(
    services: ServiceFactory,
    suffix: str,
    host: str,
    *,
    include_path: bool = False,
) -> OriginServer:
    """Create one named HTTPS parent.

    :param services: Factory owning support services and their cleanup.
    :param suffix: Suffix used by this test step.
    :param host: HTTP host name used for the request.
    :param include_path: Path to the include file.
    """

    origin = services.origin(
        f"server-{suffix}",
        ssl=True,
        clientkey=SSL_DIRECTORY / f"server-{suffix}.key",
        clientcert=SSL_DIRECTORY / f"server-{suffix}.pem",
    )
    origin.add_response(
        {"headers": f"GET / HTTP/1.1\r\nHost: {host}\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": f"{suffix} ok"
        },
    )
    if include_path:
        origin.add_response(
            {"headers": f"GET /path HTTP/1.1\r\nHost: {host}\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "path bar ok"
            },
        )
    return origin


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve parent and remap names to loopback.

    :param services: Factory owning support services and their cleanup.
    """

    dns = services.dns("dns")
    dns.add_records({
        "foo.com.": ["127.0.0.1"],
        "bar.com.": ["127.0.0.1"],
        "parent.": ["127.0.0.1"],
        "strategy.": ["127.0.0.1"],
    })
    return dns


def configure_ats(ats_factory: ATSFactory, *, _bar: OriginServer, _dns: DNSServer, _foo: OriginServer) -> ATS:
    """Configure parent.config and equivalent first-live strategy failover.

    :param _bar: Test-local bar configured by the test.
    :param _dns: Test-local dns configured by the test.
    :param _foo: Test-local foo configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|ssl|parent_select|next_hop",
            "proxy.config.ssl.client.verify.server.policy": "ENFORCED",
            "proxy.config.ssl.client.verify.server.properties": "NAME",
            "proxy.config.url_remap.pristine_host_hdr": 0,
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http.connect.down.policy": 1,
        })
    ats.remap_config.add_lines(
        (
            "map http://parent https://parent",
            "map http://strategy https://strategy @strategy=strat",
            "map http://parent_prist https://parent "
            "@plugin=conf_remap.so @pparam=proxy.config.url_remap.pristine_host_hdr=1",
            "map http://strategy_prist https://strategy @strategy=strat "
            "@plugin=conf_remap.so @pparam=proxy.config.url_remap.pristine_host_hdr=1",
        ))
    ats.parent_config.add_line(
        f'dest_domain=. port=443 parent="foo.com:{_foo.https_port}|1;bar.com:{_bar.https_port}|1" '
        'parent_retry=simple_retry parent_is_proxy=false go_direct=false simple_server_retry_responses="404" '
        "host_override=true")
    ats.write_config_file(
        "strategies.yaml",
        "groups:\n"
        "  - &gstrat\n"
        "    - host: foo.com\n"
        "      protocol:\n"
        "        - scheme: https\n"
        f"          port: {_foo.https_port}\n"
        "      weight: 1.0\n"
        "    - host: bar.com\n"
        "      protocol:\n"
        "        - scheme: https\n"
        f"          port: {_bar.https_port}\n"
        "      weight: 1.0\n"
        "strategies:\n"
        "  - strategy: strat\n"
        "    policy: first_live\n"
        "    go_direct: false\n"
        "    parent_is_proxy: false\n"
        "    ignore_self_detect: true\n"
        "    host_override: true\n"
        "    groups:\n"
        "      - *gstrat\n"
        "    scheme: https\n"
        "    failover:\n"
        "      ring_mode: exhaust_ring\n"
        "      response_codes:\n"
        "        - 404\n",
    )
    return ats


def request(host: str, *, _ats: ATS, _curl: Curl) -> str:
    """Request one parent selection path through ATS as a forward proxy.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    """

    result = _curl.run_for(
        _ats,
        f"--silent --location --proxy 'localhost:{_ats.http_port}' 'http://{host}/path'",
    )
    assert result.returncode == 0, result.output
    return result.stdout


def test_tls_sni_parent_failover(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """HTTPS parent failover preserves the selected parent's SNI name.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _foo = configure_origin(services, "foo", "foo.com")
    _bar = configure_origin(services, "bar", "bar.com", include_path=True)
    _dns = configure_dns(services)
    _ats = configure_ats(ats_factory, _bar=_bar, _dns=_dns, _foo=_foo)

    _foo.start()
    _bar.start()
    _dns.start()
    _ats.start()
    for host in ("parent", "strategy", "parent_prist", "strategy_prist"):
        assert "path bar ok" in request(host, _ats=_ats, _curl=curl)
