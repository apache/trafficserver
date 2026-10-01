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
import json
import shutil

import pytest

from tools.uranium.services import ATS, ATSFactory, CommandResult, Curl, HttpBinServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


def configure_httpbin(services: ServiceFactory) -> HttpBinServer:
    """Create the HTTP behavior origin.

    :param services: Factory used to create the httpbin origin.
    """

    return services.httpbin("httpbin")


def configure_ats(ats_factory: ATSFactory, *, _httpbin: HttpBinServer) -> ATS:
    """Terminate HTTP/2, add Via headers, and configure access logging.

    :param ats_factory: Factory used to create the Traffic Server process.

    :param _httpbin: Test-local httpbin configured by the test.
    """

    ats = ats_factory.create("ts", enable_tls=True, enable_cache=False)
    ats.add_default_ssl_files()
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_httpbin.port}")
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.http.insert_request_via_str": 1,
            "proxy.config.http.insert_response_via_str": 1,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
            "proxy.config.log.max_secs_per_buffer": 1,
        })
    ats.set_logging_yaml(
        {
            "logging":
                {
                    "formats":
                        [
                            {
                                "name": "access",
                                "format": "[%<cqtn>] %<cqhm> %<pqu> %<cqpv> %<cqssv> %<cqssc> %<crc> %<pssc> %<pscl>",
                            }
                        ],
                    "logs": [{
                        "filename": "access",
                        "format": "access"
                    }],
                }
        })
    return ats


def request(path: str, *arguments: str, _ats: ATS, _curl: Curl) -> CommandResult:
    """Send one verbose HTTP/2 request through ATS.

    :param path: Request path appended to the Traffic Server URL.
    :param arguments: Additional curl command-line arguments.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    """

    result = _curl.run_for(
        _ats,
        (f"--verbose --silent --insecure --http2 {shlex.join(arguments)} "
         f"'https://127.0.0.1:{_ats.https_port}{path}'"),
    )
    assert result.returncode == 0, result.output
    assert "HTTP/2 200" in result.stderr
    return result


def test_httpbin(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """HTTP/2 correctly proxies common httpbin response behaviors.

    :param ats_factory: Factory used to create the Traffic Server process.
    :param services: Factory used to create the httpbin origin.
    :param curl: Curl client used to send HTTP/2 requests.
    """
    _httpbin = configure_httpbin(services)
    _ats = configure_ats(ats_factory, _httpbin=_httpbin)

    if not curl.supports("http2"):
        pytest.skip("curl with HTTP/2 support is required")
    if shutil.which("cksum") is None:
        pytest.skip("cksum is required")
    _httpbin.start()
    _ats.start()

    basic = request("/get", _ats=_ats, _curl=curl)
    assert json.loads(basic.stdout)["url"].endswith("/get")
    assert "via:" in basic.stderr.lower()

    empty = request("/bytes/0", _ats=_ats, _curl=curl)
    assert empty.stdout == ""
    assert "content-length: 0" in empty.stderr.lower()

    stream = _ats.run_shell(f"curl -sk --http2 https://127.0.0.1:{_ats.https_port}/stream-bytes/102400?seed=0 | cksum")
    assert stream.returncode == 0, stream.output
    assert stream.stdout == "3197674613 102400\n"

    post = request("/post", "--data", "key=value", "--header", "Expect: 100-continue", "--max-time", "5", _ats=_ats, _curl=curl)
    assert "HTTP/2 100" in post.stderr
    assert json.loads(post.stdout)["form"] == {"key": ["value"]}

    access = wait_for_file_lines(_ats.log_directory / "access.log", r"POST .*?/post", 1)
    for fragment in ("GET http://127.0.0.1:", "/bytes/0 http/2", "/stream-bytes/102400?seed=0 http/2"):
        assert fragment in access
