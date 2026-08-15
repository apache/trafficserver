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


class ServerOutputScenario:
    """Verify traffic_ctl server status and connection-tracker JSON output."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Use a small fixed event-thread pool."""

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.exec_thread.autoconfig.enabled": 0,
            "proxy.config.exec_thread.limit": 4,
        })
        return ats

    def server_status(self) -> dict[str, object]:
        """Read and parse traffic_ctl server status."""

        result = self._ats.traffic_ctl("server", "status")
        assert result.returncode == 0, result.output
        return json.loads(result.stdout)

    def connection_tracker(self, table: str | None = None) -> dict[str, object]:
        """Invoke the connection-tracker RPC for the selected table."""

        arguments = ["rpc", "invoke", "get_connection_tracker_info"]
        if table is not None:
            arguments.extend(["--params", f"table: {table}"])
        arguments.extend(["--format", "json"])
        result = self._ats.traffic_ctl(*arguments)
        assert result.returncode == 0, result.output
        return json.loads(result.stdout)["result"]

    def run(self) -> None:
        """Verify status changes after drain and all tracker table selectors."""

        self._ats.start()
        status = self.server_status()
        assert status["initialized_done"] == "true"
        assert status["is_ssl_handshaking_stopped"] == "false"
        assert status["is_draining"] == "false"
        assert status["is_event_system_shut_down"] == "false"

        result = self._ats.traffic_ctl("server", "drain")
        assert result.returncode == 0, result.output
        assert self.server_status()["is_draining"] == "true"

        empty = {"count": "0", "list": []}
        assert self.connection_tracker("both") == {"outbound": empty, "inbound": empty}
        assert self.connection_tracker() == {"outbound": empty}
        assert self.connection_tracker("inbound") == {"inbound": empty}
        assert self.connection_tracker("outbound") == {"outbound": empty}


def test_traffic_ctl_server_output(ats_factory: ATSFactory) -> None:
    """traffic_ctl reports server and connection-tracker state as JSON."""

    ServerOutputScenario(ats_factory).run()
