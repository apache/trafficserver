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

import time

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


def configure_origin(services: ServiceFactory, name: str) -> OriginServer:
    """Create an origin for the TLS listener smoke requests.

    :param services: Factory owning support services and their cleanup.
    :param name: Unique service or case name within this test.
    """

    origin = services.origin(name)
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_valid_ats(ats_factory: ATSFactory, name: str, origin: OriginServer) -> ATS:
    """Configure a TLS listener with the default certificate.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    :param origin: Configured origin service.
    """

    ats = ats_factory.create(name, enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update(
        {
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.multicert.exit_on_load_fail": 0,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.http_port}")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    return ats


def request(ats: ATS, *, _curl: Curl) -> None:
    """Require the configured example.com certificate and a good response.

    :param _curl: Test-local curl configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    """

    result = _curl.run_for(
        ats,
        (
            f"--silent --verbose --insecure --resolve 'example.com:{ats.https_port}:127.0.0.1' "
            f"'https://example.com:{ats.https_port}/'"),
    )
    assert result.returncode == 0, result.output
    assert "Could Not Connect" not in result.stdout
    assert "CN=example.com" in result.stderr


def check_failed_reload_retains_old_context(*, _ats_factory: ATSFactory, _curl: Curl, _services: ServiceFactory) -> None:
    """Fail a certificate reload and verify the old context still serves.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _services: Test-local services configured by the test.
    """

    origin = configure_origin(_services, "server")
    ats = configure_valid_ats(_ats_factory, "ts", origin)
    origin.start()
    ats.start()
    request(ats, _curl=_curl)

    ats.write_config_file(
        "ssl_multicert.yaml",
        "ssl_multicert:\n"
        "  - ssl_cert_name: server_does_not_exist.pem\n"
        "    ssl_key_name: server_does_not_exist.key\n"
        '  - dest_ip: "*"\n'
        "    ssl_cert_name: server.pem_doesnotexist\n"
        "    ssl_key_name: server.key\n",
    )
    reload_result = ats.traffic_ctl("config", "reload", "-t", "invalid_multicert")
    assert reload_result.returncode == 0, reload_result.output
    time.sleep(3)
    request(ats, _curl=_curl)
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert "(quic)" not in "\n".join(line for line in diagnostics.splitlines() if "ssl_multicert" in line)


def check_invalid_startup_fails(*, _ats_factory: ATSFactory) -> None:
    """Require the default exit-on-load-failure behavior.

    :param _ats_factory: Test-local ats factory configured by the test.
    """

    ats = _ats_factory.create("ts2", enable_tls=True)
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem_doesnotexist",
            "    ssl_key_name: server.key",
        ))
    ats.expect_start_failure("EMERGENCY: failed to load SSL certificate file", return_code=33)
    ats.start()


def check_parallel_loading(*, _ats_factory: ATSFactory, _curl: Curl, _services: ServiceFactory) -> None:
    """Load multiple certificate entries during startup.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param _services: Test-local services configured by the test.
    """

    origin = configure_origin(_services, "server3")
    ats = configure_valid_ats(_ats_factory, "ts3", origin)
    ats.ssl_multicert_config.add_lines((
        "  - ssl_cert_name: server.pem",
        "    ssl_key_name: server.key",
    ))
    origin.start()
    ats.start()
    request(ats, _curl=_curl)
    assert "loaded 2 certs" in ats.diags_log.read_text(errors="replace")


def test_ssl_multicert_loader(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Certificate reload failures retain old contexts and startup remains strict.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """

    check_failed_reload_retains_old_context(_ats_factory=ats_factory, _curl=curl, _services=services)
    check_invalid_startup_fails(_ats_factory=ats_factory)
    check_parallel_loading(_ats_factory=ats_factory, _curl=curl, _services=services)
