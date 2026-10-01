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
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent
TEST_TOOLS = TEST_DIRECTORY.parents[2] / "tools"
REPLAY_FILE = TEST_DIRECTORY / "replay" / "response_body.yaml"


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the origin for all response-body cases.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("server", REPLAY_FILE)


def configure_ats(ats_factory: ATSFactory, *, _server: VerifierServer) -> ATS:
    """Enable full traffic_dump body capture on HTTP/1 and HTTP/2.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_tls=True)
    if not ats.plugin_exists("traffic_dump.so"):
        pytest.skip("traffic_dump.so is not installed")
    ats.copy_to_ssl(
        TEST_DIRECTORY / "ssl" / "server.pem",
        TEST_DIRECTORY / "ssl" / "server.key",
        TEST_DIRECTORY / "ssl" / "signer.pem",
    )
    ats.ssl_multicert_config.add_lines(
        (
            "ssl_multicert:",
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ))
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "traffic_dump",
            "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
            "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            "proxy.config.url_remap.pristine_host_hdr": 1,
            "proxy.config.ssl.CA.cert.filename": str(ats.ssl_directory / "signer.pem"),
            "proxy.config.exec_thread.autoconfig.scale": 1.0,
            "proxy.config.http.host_sni_policy": 2,
            "proxy.config.ssl.TLSv1_3.enabled": 0,
            "proxy.config.ssl.client.verify.server.policy": "PERMISSIVE",
        })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.http_port}")
    ats.plugin_config.add_line(f"traffic_dump.so --logdir {ats.log_directory} --sample 1 --limit 1000000000 -b")
    return ats


def run_traffic(*, _ats: ATS, _services: ServiceFactory) -> None:
    """Replay the four HTTP/1 and HTTP/2 response cases.

    :param _ats: Test-local ats configured by the test.
    :param _services: Test-local services configured by the test.
    """

    client = _services.verifier_client(
        "client",
        REPLAY_FILE,
        http_ports=[_ats.http_port],
        https_ports=[_ats.https_port],
        ssl_cert=TEST_DIRECTORY / "ssl" / "server_combined.pem",
        ca_cert=TEST_DIRECTORY / "ssl" / "signer.pem",
    )
    result = client.run()
    assert result.returncode == 0, result.output


def wait_for_dump(index: int, *, _dump_directory: Path) -> Path:
    """Wait for the replay file with @a index to be closed and visible.

    :param _dump_directory: Test-local dump directory configured by the test.
    :param index: Index used by this test step.
    """

    path = _dump_directory / f"{index:016d}"
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if path.is_file() and path.stat().st_size:
            return path
        time.sleep(0.1)
    raise AssertionError(f"traffic_dump did not write {path}")


def verify_dump(index: int, response_body: str | None = None, *, _ats: ATS, _dump_directory: Path) -> None:
    """Validate one dumped replay and its optional body text.

    :param _ats: Test-local ats configured by the test.
    :param _dump_directory: Test-local dump directory configured by the test.
    :param index: Index used by this test step.
    :param response_body: Response body used by this test step.
    """

    arguments: list[str | Path] = [
        sys.executable,
        TEST_DIRECTORY / "verify_replay.py",
        TEST_TOOLS / "lib" / "replay_schema.json",
        wait_for_dump(index, _dump_directory=_dump_directory),
    ]
    if response_body is not None:
        arguments.extend(("--response_body", response_body))
    result = _ats.run(*arguments)
    assert result.returncode == 0, result.output


def test_traffic_dump_response_body(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """traffic_dump serializes response bodies for HTTP/1 and HTTP/2.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _server = configure_server(services)
    _ats = configure_ats(ats_factory, _server=_server)
    _dump_directory = _ats.log_directory / "127"

    _server.start()
    _ats.start()
    run_traffic(_ats=_ats, _services=services)
    verify_dump(0, _ats=_ats, _dump_directory=_dump_directory)
    verify_dump(1, "0000000 0000001 ", _ats=_ats, _dump_directory=_dump_directory)
    verify_dump(2, '12"34', _ats=_ats, _dump_directory=_dump_directory)
    verify_dump(3, "0000000 0000001 0000002 ", _ats=_ats, _dump_directory=_dump_directory)
    assert "Dumping body bytes: true" in _ats.traffic_out.read_text(errors="replace")
