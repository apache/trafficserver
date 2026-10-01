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
import shlex

from tools.uranium.services import (
    ATS,
    ATSFactory,
    CommandResult,
    Curl,
    OriginServer,
    ServiceFactory,
    assert_matches_gold,
    wait_for_file_lines,
)

TEST_DIRECTORY = Path(__file__).parent
SSL_DIRECTORY = TEST_DIRECTORY / "ssl"


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the clear-text origin used after successful handshakes.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
    )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer) -> ATS:
    """Configure strict global verification and the SNI exception matrix.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl(SSL_DIRECTORY / "server.pem", SSL_DIRECTORY / "server.key", SSL_DIRECTORY / "signer.pem")
    ats.set_ssl_multicert_yaml({"ssl_multicert": [{"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}]})
    ats.records.update(
        {
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.ssl.client.certification_level": 2,
            "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.ssl.TLSv1_3.enabled": 0,
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}/")
    ats.write_config_file(
        "sni.yaml",
        "sni:\n"
        "  - fqdn: bob.bar.com\n"
        "    verify_client: NONE\n"
        "  - fqdn: bob.com\n"
        "    verify_client: STRICT\n"
        "  - fqdn: '*.foo.com'\n"
        "    verify_client: NONE\n"
        "  - fqdn: '*.bar.com'\n"
        "    verify_client: STRICT\n",
    )
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "testformat",
                        "format": "%<pssc> %<pquc> %<pscert> %<cscert>"
                    }],
                    "logs": [{
                        "mode": "ascii",
                        "format": "testformat",
                        "filename": "squid"
                    }],
                }
        })
    return ats


def request(
        host: str,
        case_number: int,
        certificate: str | None = None,
        key: str | None = None,
        *,
        _ats: ATS,
        _curl: Curl) -> CommandResult:
    """Connect to one SNI name with optional client certificate material.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param host: HTTP host name used for the request.
    :param case_number: Case number used by this test step.
    :param certificate: Certificate used by this test step.
    :param key: Key used by this test step.
    """

    arguments = ["--tls-max", "1.2", "--insecure"]
    if certificate is not None and key is not None:
        arguments.extend(("--cert", str(SSL_DIRECTORY / certificate), "--key", str(SSL_DIRECTORY / key)))
    arguments.extend((
        "--resolve",
        f"{host}:{_ats.https_port}:127.0.0.1",
        f"https://{host}:{_ats.https_port}/case{case_number}",
    ))
    return _curl.run_for(
        _ats,
        shlex.join(arguments),
    )


def test_tls_client_verify(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Global and SNI client-certificate policies produce the expected handshakes and logs.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin)

    _origin.start()
    _ats.start()
    cases = (
        ("foo.com", 1, None, None, False),
        ("foo.com", 2, "server.pem", "server.key", False),
        ("foo.com", 3, "signed-foo.pem", "signed-foo.key", True),
        ("bob.bar.com", 4, None, None, True),
        ("bob.bar.com", 5, "signed-bob-bar.pem", "signed-bar.key", True),
        ("bob.bar.com", 6, "server.pem", "server.key", True),
        ("bob.foo.com", 7, None, None, True),
        ("bob.foo.com", 8, "signed-bob-foo.pem", "signed-foo.key", True),
        ("bob.foo.com", 9, "server.pem", "server.key", True),
        ("bar.com", 10, None, None, False),
        ("bar.com", 11, "signed-bar.pem", "signed-bar.key", True),
        ("bar.com", 12, "server.pem", "server.key", False),
        ("bob.com", 13, None, None, False),
        ("bob.foo.com", 14, None, None, True),
    )
    for host, case_number, certificate, key, should_succeed in cases:
        result = request(host, case_number, certificate, key, _ats=_ats, _curl=curl)
        if should_succeed:
            assert result.returncode == 0, result.output
        else:
            assert result.returncode != 0, result.output

    access_log = wait_for_file_lines(_ats.log_directory / "squid.log", r"^404 ", 9)
    assert_matches_gold(access_log, TEST_DIRECTORY / "gold" / "clientcert-accesslog.gold")
