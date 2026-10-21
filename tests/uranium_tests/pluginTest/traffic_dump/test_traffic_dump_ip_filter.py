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

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer

TEST_DIRECTORY = Path(__file__).parent
TEST_TOOLS = TEST_DIRECTORY.parents[2] / "tools"
TRAFFIC_DUMP_IP_FILTER__server_replay = "replay/traffic_dump_ip_filter_server.yaml"

TRAFFIC_DUMP_IP_FILTER__client_replay = "replay/traffic_dump.yaml"


def configure_server(services: ServiceFactory) -> VerifierServer:
    """Create the common request origin.

    :param services: Factory owning support services and their cleanup.
    """

    return services.verifier_server("server", TRAFFIC_DUMP_IP_FILTER__server_replay)


def configure_case(ats_factory: ATSFactory, name: str, ip_filter: str, *, _server: VerifierServer) -> tuple[ATS, Path]:
    """Configure one ATS instance with @a ip_filter.

    :param _server: Test-local server configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param name: Unique service or case name within this test.
    :param ip_filter: Ip filter used by this test step.
    """

    ats = ats_factory.create(name)
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "traffic_dump",
    })
    ats.remap_config.add_line(f"map / http://127.0.0.1:{_server.http_port}")
    ats.plugin_config.add_line(f"traffic_dump.so --logdir {ats.log_directory} --sample 1 --limit 1000000000 -4 {ip_filter}")
    return ats, ats.log_directory / "127" / "0000000000000000"


def run_client(ats: ATS, number: int, *, _services: ServiceFactory) -> ProcessService:
    """Replay the filtered transaction through @a ats.

    :param _services: Test-local services configured by the test.
    :param ats: Traffic Server instance configured or queried by this step.
    :param number: Number used by this test step.
    """

    client = _services.verifier_client(
        f"client{number}", TRAFFIC_DUMP_IP_FILTER__client_replay, http_ports=[ats.http_port], keys="1")
    result = client.run()
    assert result.returncode == 0, result.output
    return client


def test_traffic_dump_ip_filter(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """traffic_dump filters IPv4 connections and rejects invalid filter text.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _server = configure_server(services)
    _cases = [
        configure_case(ats_factory, "ts1", "127.0.0.1", _server=_server),
        configure_case(ats_factory, "ts2", "1.2.3.4", _server=_server),
        configure_case(ats_factory, "ts3", "this_is_not_a_valid_ip_string", _server=_server),
    ]
    if not _cases[0][0].plugin_exists("traffic_dump.so"):
        pytest.skip("traffic_dump.so is required")

    _server.start()
    for ats, _ in _cases:
        ats.start()

    matching, matching_dump = _cases[0]
    run_client(matching, 1, _services=services)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and not matching_dump.is_file():
        time.sleep(0.1)
    assert matching_dump.is_file()
    verify = matching.run(
        sys.executable,
        TEST_DIRECTORY / "verify_replay.py",
        TEST_TOOLS / "lib" / "replay_schema.json",
        matching_dump,
    )
    assert verify.returncode == 0, verify.output
    assert "Filtering to only dump connections with ip: 127.0.0.1" in matching.traffic_out.read_text(errors="replace")

    filtered, filtered_dump = _cases[1]
    run_client(filtered, 2, _services=services)
    time.sleep(0.5)
    assert not filtered_dump.exists()
    assert "Filtering to only dump connections with ip: 1.2.3.4" in filtered.traffic_out.read_text(errors="replace")

    invalid, invalid_dump = _cases[2]
    run_client(invalid, 3, _services=services)
    time.sleep(0.5)
    assert not invalid_dump.exists()
    assert "Problems parsing IP filter address argument: this_is_not_a_valid_ip_string" in invalid.diags_log.read_text(
        errors="replace")
