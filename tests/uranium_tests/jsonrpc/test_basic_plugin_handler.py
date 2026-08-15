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

from typing import Any
import json

from tools.uranium.services import ATS, ATSFactory


class JsonRpcPluginHandlerScenario:
    """Exercise methods registered by the JSON-RPC test plugin."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._request_id = 0
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Install and load the custom JSON-RPC handler plugin."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc|jsonrpc_plugin_handler_test",
            })
        ats.copy_custom_plugin("plugins/.libs/jsonrpc_plugin_handler_test.so")
        ats.plugin_config.add_line("jsonrpc_plugin_handler_test.so")
        return ats

    def rpc(self, method: str, params: object | None = None) -> dict[str, Any]:
        """Invoke one JSON-RPC method and return its decoded response."""

        self._request_id += 1
        request: dict[str, object] = {
            "jsonrpc": "2.0",
            "id": str(self._request_id),
            "method": method,
        }
        if params is not None:
            request["params"] = params
        result = self._ats.rpc(request)
        assert result.returncode == 0, result.output
        response = json.loads(result.stdout)
        assert "error" not in response, response
        return response

    def check_registration(self) -> None:
        """Verify both custom handlers are published."""

        response = self.rpc("show_registered_handlers")
        rendered = json.dumps(response)
        assert "test_join_hosts_method" in rendered
        assert "test_join_hosts_notification" in rendered

    def check_join_method(self) -> None:
        """Verify the synchronous custom method response."""

        response = self.rpc(
            "test_join_hosts_method",
            {"hosts": ["yahoo.com", "aol.com", "vz.com"]},
        )
        assert response["result"]["join"] == "yahoo.comaol.comvz.com"

    def check_task_thread_io(self) -> None:
        """Verify plugin state creation and update on ET_TASK."""

        hosts = [
            {
                "name": "brbzull",
                "status": "up"
            },
            {
                "name": "brbzull1",
                "status": "down"
            },
            {
                "name": "brbzull3",
                "status": "up"
            },
            {
                "name": "brbzull4",
                "status": "down"
            },
            {
                "name": "yahoo",
                "status": "down"
            },
            {
                "name": "trafficserver",
                "status": "down"
            },
        ]
        created = self.rpc("test_io_on_et_task", {"hosts": hosts})["result"]
        assert created["addedHosts"] == "6"
        assert created["updatedHosts"] == "0"
        updated = self.rpc(
            "test_io_on_et_task",
            {"hosts": [{
                "name": "yahoo",
                "status": "up"
            }]},
        )["result"]
        assert updated["addedHosts"] == "0"
        assert updated["updatedHosts"] == "1"

    def check_privileges(self) -> None:
        """Verify each custom handler's service-descriptor privilege flag."""

        methods = self.rpc("get_service_descriptor")["result"]["methods"]
        privileges = {method["name"]: method["privileged"] for method in methods}
        assert privileges["test_join_hosts_method"] == "1"
        assert privileges["test_io_on_et_task"] == "1"
        assert privileges["test_join_hosts_notification"] == "0"

    def run(self) -> None:
        """Start ATS and exercise every plugin handler behavior."""

        self._ats.start()
        traffic_out = self._ats.traffic_out.read_text(errors="replace")
        assert "Test Plugin Initialized." in traffic_out
        assert "test_join_hosts_method successfully registered" in traffic_out
        assert "test_join_hosts_notification successfully registered" in traffic_out
        self.check_registration()
        self.check_registration()
        self.check_join_method()
        self.check_task_thread_io()
        self.check_privileges()


def test_basic_plugin_handler(ats_factory: ATSFactory) -> None:
    """A plugin can register, advertise, and execute JSON-RPC handlers."""

    JsonRpcPluginHandlerScenario(ats_factory).run()
