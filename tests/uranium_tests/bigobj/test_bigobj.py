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

import pytest

from tools.uranium.services import ATS, ATSFactory, Curl

OBJECT_BYTES = 102_400


def write_push_file(run_directory: Path) -> Path:
    """Write the embedded HTTP response consumed by the PUSH method.

    :param run_directory: Scenario directory in which to create the body.
    """

    header = f"HTTP/1.1 200 OK\r\nContent-length: {OBJECT_BYTES}\r\n\r\n"
    path = run_directory / "objfile"
    path.write_text(header + ("x" * OBJECT_BYTES))
    return path


def configure_ats(ats_factory: ATSFactory, name: str, *, push_enabled: bool, _curl: Curl) -> ATS:
    """Configure one cache with PUSH either enabled or disabled.

    :param ats_factory: Factory that owns the new ATS instance.
    :param name: Unique process name for the ATS instance.
    :param push_enabled: Whether ATS accepts the PUSH method.

    :param _curl: Test-local curl configured by the test.
    """

    ats = ats_factory.create(name, enable_tls=True)
    server_ports = (f"{ats.http_port} {ats.ipv6_port}:ipv6 "
                    f"{ats.https_port}:ssl {ats.ipv6_https_port}:ssl:ipv6")
    records: dict[str, object] = {
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "http|dns|cache",
        "proxy.config.http.cache.required_headers": 0,
        "proxy.config.proxy_name": "Poxy_Proxy",
        "proxy.config.url_remap.remap_required": 0,
    }
    if not _curl.uses_uds:
        records["proxy.config.http.server_ports"] = server_ports
    if push_enabled:
        records["proxy.config.http.push_method_enabled"] = 1
    ats.records.update(records)
    ats.remap_config.add_lines(
        (
            f"map https://localhost:{ats.https_port} http://localhost:{ats.http_port}",
            f"map https://localhost:{ats.ipv6_https_port} http://localhost:{ats.http_port}",
        ))
    return ats


def push(ats: ATS, *, _curl: Curl, _push_file: Path) -> str:
    """PUSH the object into ATS and return curl's diagnostics.

    :param ats: ATS instance that receives the pushed object.

    :param _curl: Test-local curl configured by the test.
    :param _push_file: Test-local push file configured by the test.
    """

    result = _curl.run_for(
        ats,
        (
            "--verbose --header 'Content-Type: application/octet-stream' "
            f"--data-binary '@{_push_file}' --request PUSH "
            f"http://localhost:{ats.http_port}/bigobj "
            f"--header 'Content-Length: {_push_file.stat().st_size}'"),
        timeout=60,
    )
    assert result.returncode == 0, result.output
    return result.output


def get(arguments: str, *, _curl: Curl, _enabled: ATS) -> str:
    """Fetch the pushed object with the requested curl options.

    :param arguments: Shell-style curl arguments for the retrieval.

    :param _curl: Test-local curl configured by the test.
    :param _enabled: Test-local enabled configured by the test.
    """

    result = _curl.run_for(
        _enabled,
        f"--verbose --output /dev/null {arguments}",
        timeout=60,
    )
    assert result.returncode == 0, result.output
    assert f"content-length: {OBJECT_BYTES}" in result.output.lower()
    return result.output


def test_bigobj(ats_factory: ATSFactory, curl: Curl) -> None:
    """Large PUSH objects survive HTTP/1.1, HTTP/2, TLS, and address-family changes.

    :param ats_factory: Factory that owns the scenario's ATS instances.
    :param curl: Curl client used by the scenario.
    """

    if not Curl.supports("http2"):
        pytest.skip("curl HTTP/2 support is required")
    _push_file = write_push_file(ats_factory.run_directory)
    _enabled = configure_ats(ats_factory, "ts1", push_enabled=True, _curl=curl)
    _disabled = configure_ats(ats_factory, "ts2", push_enabled=False, _curl=curl)

    _enabled.start()
    assert "HTTP/1.1 201 Created" in push(_enabled, _curl=curl, _push_file=_push_file)
    cleartext_options = "" if curl.uses_uds else "--ipv4"
    assert "HTTP/1.1 200 OK" in get(
        f"{cleartext_options} --http1.1 http://localhost:{_enabled.http_port}/bigobj", _curl=curl, _enabled=_enabled)
    if not curl.uses_uds:
        assert "HTTP/1.1 200 OK" in get(
            f"--ipv4 --http1.1 --insecure https://localhost:{_enabled.https_port}/bigobj", _curl=curl, _enabled=_enabled)
        assert "HTTP/2 200" in get(
            f"--ipv4 --http2 --insecure https://localhost:{_enabled.https_port}/bigobj", _curl=curl, _enabled=_enabled)
        assert "HTTP/2 200" in get(
            f"--ipv6 --http2 --insecure https://localhost:{_enabled.ipv6_https_port}/bigobj", _curl=curl, _enabled=_enabled)
    _disabled.start()
    assert "403 Access Denied" in push(_disabled, _curl=curl, _push_file=_push_file)
