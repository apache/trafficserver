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
import subprocess
import sys

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl, HttpBinServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_httpbin(services: ServiceFactory) -> HttpBinServer:
    """Create the common `/ip` origin.

    :param services: Factory owning support services and their cleanup.
    """

    return services.httpbin("httpbin")


def configure_ats(ats_factory: ATSFactory, *, _httpbin: HttpBinServer) -> ATS:
    """Configure HTTP/TLS routes and the four-field access log.

    :param _httpbin: Test-local httpbin configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    ats.add_default_ssl_files()
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "snowflake|http",
    })
    ats.remap_config.add_lines(
        (
            f"map http://127.0.0.1:{ats.http_port} http://127.0.0.1:{_httpbin.port}/ip",
            f"map https://127.0.0.1:{ats.https_port} http://127.0.0.1:{_httpbin.port}/ip",
            f"map https://reallyreallyreallyreallylong.com http://127.0.0.1:{_httpbin.port}/ip",
        ))
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats": [{
                        "name": "custom",
                        "format": "%<psfid> %<ccid> %<ctid> %<cssn>"
                    }],
                    "logs": [{
                        "filename": "test_new_log_flds",
                        "format": "custom"
                    }],
                }
        })
    return ats


def request(*arguments: str, _ats: ATS, _curl: Curl) -> None:
    """Run curl and require a successful transaction.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param arguments: Arguments used by this test step.
    """

    result = _curl.run_for(
        _ats,
        shlex.join(arguments),
    )
    assert result.returncode == 0, result.output


def test_new_log_flds(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The new log fields describe process, connection, transaction, and SNI state.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    if not Curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    _httpbin = configure_httpbin(services)
    _ats = configure_ats(ats_factory, _httpbin=_httpbin)

    _httpbin.start()
    _ats.start()
    http_url = f"http://127.0.0.1:{_ats.http_port}"
    request("--verbose", http_url, _ats=_ats, _curl=curl)
    request("--verbose", http_url, _ats=_ats, _curl=curl)
    request("--http1.1", "--verbose", http_url, http_url, _ats=_ats, _curl=curl)
    expected_lines = 4
    if not curl.uses_uds:
        https_url = f"https://127.0.0.1:{_ats.https_port}"
        request("--http2", "--insecure", "--verbose", https_url, https_url, _ats=_ats, _curl=curl)
        hostname = "reallyreallyreallyreallylong.com"
        request(
            "--http2",
            "--insecure",
            "--verbose",
            "--resolve",
            f"{hostname}:{_ats.https_port}:127.0.0.1",
            f"https://{hostname}:{_ats.https_port}",
            _ats=_ats,
            _curl=curl)
        expected_lines = 7

    log_path = _ats.log_directory / "test_new_log_flds.log"
    content = wait_for_file_lines(log_path, r"^\S+ \d+ \d+ \S+$", expected_lines, timeout=60)
    observer = subprocess.run(
        (sys.executable, TEST_DIRECTORY / "new_log_flds_observer.py"),
        input=content,
        capture_output=True,
        text=True,
        check=False,
    )
    assert observer.returncode == 0, observer.stdout + observer.stderr
