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
"""Verify ATS TLS 1.3 early-data handling for HTTP/1 and HTTP/2."""

from pathlib import Path
import re
import shutil
import subprocess
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, OriginServer, ServiceFactory

TEST_DIRECTORY = Path(__file__).parent


def require_openssl() -> None:
    """Skip when the command-line client predates TLS 1.3 early data."""

    output = subprocess.check_output(("openssl", "version"), text=True)
    match = re.search(r"\d+(?:\.\d+)+", output)
    version = tuple(int(part) for part in match.group().split(".")) if match else ()
    if version < (1, 1, 1):
        pytest.skip("OpenSSL 1.1.1 or newer is required")


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create ordinary, early GET, early POST, and multiplexed responses.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("origin")
    for path, body in (
        ("/", "curl test"),
        ("/early_get", "early data accepted"),
        ("/early_multi_1", "early data accepted multi_1"),
        ("/early_multi_2", "early data accepted multi_2"),
        ("/early_multi_3", "early data accepted multi_3"),
    ):
        origin.add_response(
            {"headers": f"GET {path} HTTP/1.1\r\nHost: {{%Host}}\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": body
            },
        )
    origin.add_response(
        {
            "headers": ("POST /early_post HTTP/1.1\r\nHost: {%Host}\r\nContent-Length: 11\r\n\r\n"),
            "body": "knock knock",
        },
        {
            "headers": ("HTTP/1.1 200 OK\r\nServer: uServer\r\nConnection: close\r\n"
                        "Transfer-Encoding: chunked\r\n\r\n"),
            "body": "",
        },
    )
    return origin


def configure_ats(
        ats_factory: ATSFactory, name: str, *, max_early_data: int, sni_name: str, sni_value: int, _origin: OriginServer) -> ATS:
    """Configure one global policy with an opposing SNI override.

    :param _origin: Test-local origin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    :param max_early_data: Max early data used by this test step.
    :param sni_name: Sni name used by this test step.
    :param sni_value: Sni value used by this test step.
    """

    ats = ats_factory.create(name, enable_tls=True)
    ats.copy_to_ssl(TEST_DIRECTORY / "ssl" / "server.pem", TEST_DIRECTORY / "ssl" / "server.key")
    ats.set_ssl_multicert_yaml({"ssl_multicert": [{"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"},]})
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|ssl_early_data|ssl",
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 8,
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.server.max_early_data": max_early_data,
            "proxy.config.ssl.server.allow_early_data_params": 0,
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_origin.port}")
    ats.write_config_file(
        "sni.yaml",
        f"sni:\n- fqdn: {sni_name}\n  server_max_early_data: {sni_value}\n",
    )
    return ats


def configure_client_files(run_directory: Path) -> Path:
    """Copy mutable session and early-data inputs into the test sandbox.

    :param run_directory: Run directory used by this test step.
    """

    directory = run_directory / "early-data-client"
    directory.mkdir()
    for filename in (
            "early_h1_get.txt",
            "early_h1_post.txt",
            "early_h2_get.txt",
            "early_h2_post.txt",
            "early_h2_multi1.txt",
            "early_h2_multi2.txt",
    ):
        shutil.copy2(TEST_DIRECTORY / filename, directory / filename)
    return directory


def run_client(ats: ATS, http_version: str, case: str, *, sni: str | None = None, _client_directory: Path) -> CommandResult:
    """Run the bespoke OpenSSL early-data driver.

    :param _client_directory: Test-local client directory configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param http_version: Http version used by this test step.
    :param case: Case used by this test step.
    :param sni: Sni used by this test step.
    """

    arguments: list[str | Path] = [
        sys.executable,
        TEST_DIRECTORY / "test-0rtt-s_client.py",
        "--ats-port",
        str(ats.https_port),
        "--http-version",
        http_version,
        "--test-name",
        case,
        "--run-dir",
        _client_directory,
    ]
    if sni is not None:
        arguments.extend(("--server-name", sni))
    result = ats.run(*arguments, timeout=10)
    assert result.returncode == 0, result.output
    return result


def assert_output(result: CommandResult, *, contains: tuple[str, ...] = (), excludes: tuple[str, ...] = ()) -> None:
    """Verify expected and prohibited early-data response fragments.

    :param result: Completed command result to validate.
    :param contains: Required output fragments.
    :param excludes: Forbidden output fragments.
    """

    for value in contains:
        assert value in result.output, result.output
    for value in excludes:
        assert value not in result.output, result.output


def verify_basic_request(*, _curl: Curl, _enabled: ATS) -> None:
    """Confirm an ordinary full-handshake request reaches the origin.

    :param _curl: Test-local curl configured by the test.
    :param _enabled: Test-local enabled configured by the test.
    """

    ats = _enabled
    result = _curl.run_for(
        ats,
        (
            f"--insecure --silent --show-error --resolve 'example.com:{ats.https_port}:127.0.0.1' "
            f"'https://example.com:{ats.https_port}/'"),
    )
    assert result.returncode == 0, result.output
    assert_output(result, contains=("curl test",), excludes=("early data accepted",))


def verify_enabled_policy(*, _client_directory: Path, _enabled: ATS) -> None:
    """Accept safe data, reject POST, and handle multiplexed HTTP/2 streams.

    :param _client_directory: Test-local client directory configured by the test.
    :param _enabled: Test-local enabled configured by the test.
    """

    forbidden = ("curl test",)
    assert_output(
        run_client(_enabled, "h1", "get", _client_directory=_client_directory),
        contains=("early data accepted",),
        excludes=forbidden)
    assert_output(
        run_client(_enabled, "h1", "post", _client_directory=_client_directory),
        contains=("HTTP/1.1 425 Too Early",),
        excludes=("curl test", "early data accepted"),
    )
    assert_output(
        run_client(_enabled, "h2", "get", _client_directory=_client_directory),
        contains=("early data accepted",),
        excludes=forbidden)
    assert_output(
        run_client(_enabled, "h2", "post", _client_directory=_client_directory),
        contains=(":status 425",),
        excludes=("curl test", "early data accepted"),
    )
    assert_output(
        run_client(_enabled, "h2", "multi1", _client_directory=_client_directory),
        contains=(
            "early data accepted multi_1",
            "early data accepted multi_2",
            "early data accepted multi_3",
        ),
        excludes=forbidden,
    )
    assert_output(
        run_client(_enabled, "h2", "multi2", _client_directory=_client_directory),
        contains=("early data accepted multi_1", ":status 425", "early data accepted multi_3"),
        excludes=forbidden,
    )
    assert_output(
        run_client(_enabled, "h1", "get", sni="example.com", _client_directory=_client_directory),
        contains=("early data accepted",),
        excludes=forbidden,
    )
    assert_output(
        run_client(_enabled, "h1", "get", sni="example-no.com", _client_directory=_client_directory),
        excludes=("curl test", "early data accepted"),
    )


def verify_disabled_policy(*, _client_directory: Path, _disabled: ATS) -> None:
    """Reject early data globally unless its SNI policy enables it.

    :param _client_directory: Test-local client directory configured by the test.
    :param _disabled: Test-local disabled configured by the test.
    """

    for sni in (None, "example.com"):
        assert_output(
            run_client(_disabled, "h1", "get", sni=sni, _client_directory=_client_directory),
            excludes=("curl test", "early data accepted"),
        )
    assert_output(
        run_client(_disabled, "h1", "get", sni="example-yes.com", _client_directory=_client_directory),
        contains=("early data accepted",),
        excludes=("curl test",),
    )


def test_tls_0rtt_server(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """ATS applies HTTP safety and SNI policy to TLS 1.3 early data.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    require_openssl()
    _origin = configure_origin(services)
    _enabled = configure_ats(ats_factory, "enabled", max_early_data=16384, sni_name="example-no.com", sni_value=0, _origin=_origin)
    _disabled = configure_ats(
        ats_factory, "disabled", max_early_data=0, sni_name="example-yes.com", sni_value=16384, _origin=_origin)
    _client_directory = configure_client_files(ats_factory.run_directory)

    _origin.start()
    _enabled.start()
    _disabled.start()
    verify_basic_request(_curl=curl, _enabled=_enabled)
    verify_enabled_policy(_client_directory=_client_directory, _enabled=_enabled)
    verify_disabled_policy(_client_directory=_client_directory, _disabled=_disabled)
