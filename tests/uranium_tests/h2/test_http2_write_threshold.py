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
import sys

from tools.uranium.services import ATS, ATSFactory, DNSServer, ProcessService, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent
TEST_SSL = TEST_DIRECTORY.parents[1] / "tools" / "ssl"


def configure_dns(services: ServiceFactory) -> DNSServer:
    """Resolve example.com to the local trickle server.

    :param services: Factory owning support services and their cleanup.
    """

    return services.dns("dns", default=["127.0.0.1"])


def configure_server(services: ServiceFactory, *, _server_port: int, _write_timeout: int) -> ProcessService:
    """Create the TLS HTTP/2 server that trickles frames.

    :param _server_port: Test-local server port configured by the test.
    :param _write_timeout: Test-local write timeout configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "server",
        (
            sys.executable,
            TEST_DIRECTORY / "trickle_server.py",
            str(_server_port),
            TEST_SSL / "server.pem",
            TEST_SSL / "server.key",
            str(_write_timeout),
        ),
        ready_port=_server_port,
    )


def configure_ats(
        ats_factory: ATSFactory, *, _dns: DNSServer, _server_port: int, _write_threshold: float, _write_timeout: int) -> ATS:
    """Configure outbound H2 and the selected write thresholds.

    :param _dns: Test-local dns configured by the test.
    :param _server_port: Test-local server port configured by the test.
    :param _write_threshold: Test-local write threshold configured by the test.
    :param _write_timeout: Test-local write timeout configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.remap_config.add_line(f"map / https://example.com:{_server_port}/")
    ats.records.update(
        {
            "proxy.config.ssl.client.alpn_protocols": "h2,http/1.1",
            "proxy.config.http.server_session_sharing.pool": "thread",
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
            "proxy.config.dns.nameservers": f"127.0.0.1:{_dns.port}",
            "proxy.config.dns.resolv_conf": "NULL",
            "proxy.config.http2.write_size_threshold": _write_threshold,
            "proxy.config.http2.write_time_threshold": _write_timeout,
            "proxy.config.diags.debug.enabled": 0,
            "proxy.config.diags.debug.tags": "http",
        })
    return ats


def configure_client(services: ServiceFactory, *, _ats: ATS, _write_timeout: int) -> ProcessService:
    """Create the HTTP/2 client that measures frame delivery timing.

    :param _ats: Test-local ats configured by the test.
    :param _write_timeout: Test-local write timeout configured by the test.
    :param services: Factory owning support services and their cleanup.
    """

    return services.process(
        "client",
        (
            sys.executable,
            TEST_DIRECTORY / "trickle_client.py",
            "example.com",
            str(_ats.https_port),
            TEST_SSL / "server.pem",
            str(_write_timeout),
        ),
    )


def test_http2_write_threshold(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """proxy.config.http2.write_size_threshold flushes by size or timeout.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    write_threshold = 0.5
    write_timeout = 10
    _server_port = services.allocate_port()
    _dns = configure_dns(services)
    _server = configure_server(services, _server_port=_server_port, _write_timeout=write_timeout)
    _ats = configure_ats(
        ats_factory, _dns=_dns, _server_port=_server_port, _write_threshold=write_threshold, _write_timeout=write_timeout)
    _client = configure_client(services, _ats=_ats, _write_timeout=write_timeout)

    _dns.start()
    _server.start()
    _ats.start()
    result = _client.run(timeout=20)
    assert result.returncode == 0, result.output
