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

from tools.uranium.services import ATS, ATSFactory


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Use a small fixed event-thread pool.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update({
        "proxy.config.exec_thread.autoconfig.enabled": 0,
        "proxy.config.exec_thread.limit": 4,
    })
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
