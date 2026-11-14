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

import json
import socket

import pytest
from tools.uranium.services import ATS, ATSFactory, wait_for_metric


def configure_ats(ats_factory: ATSFactory, **records: object) -> ATS:
    """Use a small fixed event-thread pool.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param records: Extra or overridden records.yaml entries.
    """

    ats = ats_factory.create("ts")
    ats.records.update(
        {
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 4,
        }
    )
    ats.records.update(records)
    return ats


def server_status(*, _ats: ATS) -> dict[str, object]:
    """Read and parse traffic_ctl server status.

    :param _ats: Test-local ats configured by the test.
    """

    result = _ats.traffic_ctl("server", "status")
    assert result.returncode == 0, result.output
    return json.loads(result.stdout)


def connection_tracker(table: str | None = None, *, _ats: ATS) -> dict[str, object]:
    """Invoke the connection-tracker RPC for the selected table.

    :param _ats: Test-local ats configured by the test.
    :param table: Table used by this test step.
    """

    arguments = ["rpc", "invoke", "get_connection_tracker_info"]
    if table is not None:
        arguments.extend(["--params", f"table: {table}"])
    arguments.extend(["--format", "json"])
    result = _ats.traffic_ctl(*arguments)
    assert result.returncode == 0, result.output
    return json.loads(result.stdout)["result"]


def test_traffic_ctl_server_output(ats_factory: ATSFactory) -> None:
    """traffic_ctl reports server and connection-tracker state as JSON.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    status = server_status(_ats=_ats)
    assert status["initialized_done"] == "true"
    assert status["is_ssl_handshaking_stopped"] == "false"
    assert status["is_draining"] == "false"
    assert status["is_event_system_shut_down"] == "false"

    result = _ats.traffic_ctl("server", "drain")
    assert result.returncode == 0, result.output
    assert server_status(_ats=_ats)["is_draining"] == "true"

    empty = {"count": "0", "list": []}
    assert connection_tracker("both", _ats=_ats) == {"outbound": empty, "inbound": empty}
    assert connection_tracker(_ats=_ats) == {"outbound": empty}
    assert connection_tracker("inbound", _ats=_ats) == {"inbound": empty}
    assert connection_tracker("outbound", _ats=_ats) == {"outbound": empty}


@pytest.mark.parametrize("limit, expect_tracked", [(0, False), (3, True)])
def test_connection_tracker_inbound_per_client_limit(ats_factory: ATSFactory, limit: int, expect_tracked: bool) -> None:
    """Verify that inbound connections are only tracked when per-client limit is configured.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param limit: Value for proxy.config.net.per_client.max_connections_in.
    :param expect_tracked: Whether the open client connection should appear in the inbound table.
    """

    ats = configure_ats(ats_factory, **({"proxy.config.net.per_client.max_connections_in": limit} if limit > 0 else {}))
    ats.start()

    with socket.create_connection(("127.0.0.1", ats.http_port)) as client_sock:
        client_sock.sendall(b"GET / HTTP/1.1\r\nHost: test\r\n")
        wait_for_metric(ats, "proxy.process.net.connections_currently_open", 1)
        inbound = connection_tracker("inbound", _ats=ats)["inbound"]
        if expect_tracked:
            assert int(inbound["count"]) == 1
            assert len(inbound["list"]) == 1
            assert inbound["list"][0]["ip"].startswith("127.0.0.1:")
        else:
            assert inbound == {"count": "0", "list": []}
