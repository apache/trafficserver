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

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, assert_matches_gold

TEST_DIRECTORY = Path(__file__).parent


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create successful responses for the S3 and GCP paths.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    for path in ("s3-bucket", "gcp"):
        origin.add_response(
            {"headers": f"GET /{path} HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "success!"
            },
        )
    return origin


def configure_ats(ats_factory: ATSFactory, *, _origin: OriginServer, _rules: Path, _token: str) -> ATS:
    """Configure file-based AWS v4 and inline GCP authentication.

    :param _origin: Test-local origin configured by the test.
    :param _rules: Test-local rules configured by the test.
    :param _token: Test-local token configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.show_location": 0,
            "proxy.config.diags.debug.tags": "origin_server_auth",
        })
    ats.copy_to_config(_rules)
    rules_path = ats.config_directory / _rules.name
    ats.remap_config.add_lines(
        (
            f"map http://www.example.com/s3-bucket http://127.0.0.1:{_origin.port}/s3-bucket "
            f"@plugin=origin_server_auth.so @pparam=--config @pparam={rules_path}",
            f"map http://www.example.com/gcp http://127.0.0.1:{_origin.port}/gcp "
            f"@plugin=origin_server_auth.so @pparam=--access_key @pparam=1234567 "
            f"@pparam=--session_token @pparam={_token} @pparam=--version @pparam=gcpv1",
        ))
    return ats


def request(path: str, *, _ats: ATS, _curl: Curl) -> None:
    """Request one authenticated origin path.

    :param _ats: Test-local ats configured by the test.
    :param _curl: Test-local curl configured by the test.
    :param path: Resource or file path used by this operation.
    """

    result = _curl.get(
        _ats,
        f"/{path}",
        headers={"Host": "www.example.com"},
        options=f"--silent --verbose",
    )
    assert result.returncode == 0, result.output
    assert "200 OK" in result.stderr
    assert "Content-Length: 8" in result.stderr


def test_origin_server_auth(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """origin_server_auth parses long file values and inline GCP configuration.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    :param curl: Transport-aware curl command runner.
    """
    _rules = TEST_DIRECTORY / "rules" / "v4-parse-test.test_input"
    _token = next(
        line.removeprefix("session_token=").strip()
        for line in _rules.read_text().splitlines()
        if line.startswith("session_token="))
    _origin = configure_origin(services)
    _ats = configure_ats(ats_factory, _origin=_origin, _rules=_rules, _token=_token)
    if not _ats.plugin_exists("origin_server_auth.so"):
        pytest.skip("origin_server_auth.so is required")

    _origin.start()
    _ats.start()
    request("s3-bucket", _ats=_ats, _curl=curl)
    request("gcp", _ats=_ats, _curl=curl)
    gold = "origin_server_auth_parsing_ts_uds.gold" if curl.uses_uds else "origin_server_auth_parsing_ts.gold"
    assert_matches_gold(_ats.traffic_out.read_text(errors="replace"), TEST_DIRECTORY / "gold" / gold)
