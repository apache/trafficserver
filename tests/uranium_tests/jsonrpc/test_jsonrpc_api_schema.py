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
from string import Template
from typing import Any
import json

from jsonschema import Draft4Validator

from tools.uranium.services import ATS, ATSFactory


class JsonRpcApiSchemaScenario:
    """Validate representative management API requests and responses."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._test_directory = Path(__file__).parent
        self._schema_directory = self._test_directory.parents[2] / "src" / "mgmt" / "rpc" / "schema"
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure records and storage used by the API calls."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "rpc|filemanager|http|cache",
                "proxy.config.jsonrpc.filename": "jsonrpc.yaml",
            })
        ats.storage_config.add_lines(
            [
                "cache:",
                "  spans:",
                "    - name: disk-1",
                f"      path: {ats.storage_directory}",
                "      size: 512M",
            ])
        return ats

    def load_schema(self, name: str) -> dict[str, Any]:
        """Load one JSON schema from the ATS source tree."""

        return json.loads((self._schema_directory / name).read_text())

    def load_request(self, name: str, context: dict[str, str] | None = None) -> dict[str, Any]:
        """Load a request template and substitute scenario values."""

        content = (self._test_directory / "json" / name).read_text()
        if context is not None:
            content = Template(content).substitute(context)
        return json.loads(content)

    def invoke(
        self,
        request_name: str,
        *,
        context: dict[str, str] | None = None,
        params_schema: str | None = None,
        result_schema: str | None = None,
    ) -> dict[str, Any]:
        """Validate, send, and validate one API exchange."""

        request = self.load_request(request_name, context)
        Draft4Validator(self.load_schema("jsonrpc_request_schema.json")).validate(request)
        if params_schema is not None:
            Draft4Validator(self.load_schema(params_schema)).validate(request["params"])

        command = self._ats.rpc(request)
        assert command.returncode == 0, command.output
        response = json.loads(command.stdout)
        Draft4Validator(self.load_schema("jsonrpc_response_schema.json")).validate(response)
        if result_schema is not None:
            assert "result" in response, response
            Draft4Validator(self.load_schema(result_schema)).validate(response["result"])
        return response

    def check_records(self) -> None:
        """Validate record lookup and mutation requests."""

        record = {"record_name": "proxy.config.jsonrpc.filename"}
        self.invoke(
            "admin_lookup_records_req_1.json",
            context=record,
            params_schema="admin_lookup_records_params_schema.json",
        )
        self.invoke("admin_lookup_records_req_invalid_rec.json")
        self.invoke("admin_lookup_records_req_1.json", context=record)
        self.invoke("admin_lookup_records_req_multiple.json", context=record)
        self.invoke(
            "admin_lookup_records_req_metric.json",
            context={"record_name_regex": "proxy.process.http.total_client_connections_ipv4*"},
        )
        self.invoke(
            "admin_config_set_records_req.json",
            context={
                "record_name": "proxy.config.jsonrpc.filename",
                "record_value": "test_jsonrpc.yaml",
            },
        )

    def check_host_and_drain(self) -> None:
        """Validate host status and server drain methods."""

        for operation in ("up", "down"):
            self.invoke(
                "admin_host_set_status_req.json",
                context={
                    "operation": operation,
                    "host": "my.test.host.trafficserver.com"
                },
            )
        for method in ("admin_server_start_drain", "admin_server_start_drain", "admin_server_stop_drain"):
            self.invoke("method_call_no_params.json", context={"method": method})

    def check_storage_and_plugin_message(self) -> None:
        """Validate storage and plugin-message methods."""

        device = str(self._ats.storage_directory / "cache.db")
        for method in ("admin_storage_get_device_status", "admin_storage_set_device_offline"):
            self.invoke(
                "admin_storage_x_device_status_req.json",
                context={
                    "method": method,
                    "device": device
                },
            )
        self.invoke("admin_plugin_send_basic_msg_req.json", result_schema="success_response_schema.json")

    def run(self) -> None:
        """Run all schema-validated API exchanges."""

        self._ats.start()
        self.check_records()
        self.check_host_and_drain()
        self.check_storage_and_plugin_message()


def test_jsonrpc_api_schema(ats_factory: ATSFactory) -> None:
    """JSON-RPC requests and responses conform to their published schemas."""

    JsonRpcApiSchemaScenario(ats_factory).run()
