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


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure records and storage used by the API calls.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

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


def load_schema(name: str, *, _schema_directory: Path) -> dict[str, Any]:
    """Load one JSON schema from the ATS source tree.

    :param _schema_directory: Test-local schema directory configured by the test.
    :param name: Unique service or case name within this test.
    """

    return json.loads((_schema_directory / name).read_text())


def load_request(name: str, context: dict[str, str] | None = None, *, _test_directory: Path) -> dict[str, Any]:
    """Load a request template and substitute scenario values.

    :param _test_directory: Test-local test directory configured by the test.
    :param name: Unique service or case name within this test.
    :param context: Context used by this test step.
    """

    content = (_test_directory / "json" / name).read_text()
    if context is not None:
        content = Template(content).substitute(context)
    return json.loads(content)


def invoke(
        request_name: str,
        *,
        context: dict[str, str] | None = None,
        params_schema: str | None = None,
        result_schema: str | None = None,
        _ats: ATS,
        _schema_directory: Path,
        _test_directory: Path) -> dict[str, Any]:
    """Validate, send, and validate one API exchange.

    :param _ats: Test-local ats configured by the test.
    :param _schema_directory: Test-local schema directory configured by the test.
    :param _test_directory: Test-local test directory configured by the test.
    :param request_name: Request name used by this test step.
    :param context: Context used by this test step.
    :param params_schema: Params schema used by this test step.
    :param result_schema: Result schema used by this test step.
    """

    request = load_request(request_name, context, _test_directory=_test_directory)
    Draft4Validator(load_schema("jsonrpc_request_schema.json", _schema_directory=_schema_directory)).validate(request)
    if params_schema is not None:
        Draft4Validator(load_schema(params_schema, _schema_directory=_schema_directory)).validate(request["params"])

    command = _ats.rpc(request)
    assert command.returncode == 0, command.output
    response = json.loads(command.stdout)
    Draft4Validator(load_schema("jsonrpc_response_schema.json", _schema_directory=_schema_directory)).validate(response)
    if result_schema is not None:
        assert "result" in response, response
        Draft4Validator(load_schema(result_schema, _schema_directory=_schema_directory)).validate(response["result"])
    return response


def check_records(*, _ats: ATS, _schema_directory: Path, _test_directory: Path) -> None:
    """Validate record lookup and mutation requests.

    :param _ats: Test-local ats configured by the test.
    :param _schema_directory: Test-local schema directory configured by the test.
    :param _test_directory: Test-local test directory configured by the test.
    """

    record = {"record_name": "proxy.config.jsonrpc.filename"}
    invoke(
        "admin_lookup_records_req_1.json",
        context=record,
        params_schema="admin_lookup_records_params_schema.json",
        _ats=_ats,
        _schema_directory=_schema_directory,
        _test_directory=_test_directory)
    invoke(
        "admin_lookup_records_req_invalid_rec.json",
        _ats=_ats,
        _schema_directory=_schema_directory,
        _test_directory=_test_directory)
    invoke(
        "admin_lookup_records_req_1.json",
        context=record,
        _ats=_ats,
        _schema_directory=_schema_directory,
        _test_directory=_test_directory)
    invoke(
        "admin_lookup_records_req_multiple.json",
        context=record,
        _ats=_ats,
        _schema_directory=_schema_directory,
        _test_directory=_test_directory)
    invoke(
        "admin_lookup_records_req_metric.json",
        context={"record_name_regex": "proxy.process.http.total_client_connections_ipv4*"},
        _ats=_ats,
        _schema_directory=_schema_directory,
        _test_directory=_test_directory)
    invoke(
        "admin_config_set_records_req.json",
        context={
            "record_name": "proxy.config.jsonrpc.filename",
            "record_value": "test_jsonrpc.yaml",
        },
        _ats=_ats,
        _schema_directory=_schema_directory,
        _test_directory=_test_directory)


def check_host_and_drain(*, _ats: ATS, _schema_directory: Path, _test_directory: Path) -> None:
    """Validate host status and server drain methods.

    :param _ats: Test-local ats configured by the test.
    :param _schema_directory: Test-local schema directory configured by the test.
    :param _test_directory: Test-local test directory configured by the test.
    """

    for operation in ("up", "down"):
        invoke(
            "admin_host_set_status_req.json",
            context={
                "operation": operation,
                "host": "my.test.host.trafficserver.com"
            },
            _ats=_ats,
            _schema_directory=_schema_directory,
            _test_directory=_test_directory)
    for method in ("admin_server_start_drain", "admin_server_start_drain", "admin_server_stop_drain"):
        invoke(
            "method_call_no_params.json",
            context={"method": method},
            _ats=_ats,
            _schema_directory=_schema_directory,
            _test_directory=_test_directory)


def check_storage_and_plugin_message(*, _ats: ATS, _schema_directory: Path, _test_directory: Path) -> None:
    """Validate storage and plugin-message methods.

    :param _ats: Test-local ats configured by the test.
    :param _schema_directory: Test-local schema directory configured by the test.
    :param _test_directory: Test-local test directory configured by the test.
    """

    device = str(_ats.storage_directory / "cache.db")
    for method in ("admin_storage_get_device_status", "admin_storage_set_device_offline"):
        invoke(
            "admin_storage_x_device_status_req.json",
            context={
                "method": method,
                "device": device
            },
            _ats=_ats,
            _schema_directory=_schema_directory,
            _test_directory=_test_directory)
    invoke(
        "admin_plugin_send_basic_msg_req.json",
        result_schema="success_response_schema.json",
        _ats=_ats,
        _schema_directory=_schema_directory,
        _test_directory=_test_directory)


def test_jsonrpc_api_schema(ats_factory: ATSFactory) -> None:
    """JSON-RPC requests and responses conform to their published schemas.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _test_directory = Path(__file__).parent
    _schema_directory = _test_directory.parents[2] / "src" / "mgmt" / "rpc" / "schema"
    _ats = configure_ats(ats_factory)

    _ats.start()
    check_records(_ats=_ats, _schema_directory=_schema_directory, _test_directory=_test_directory)
    check_host_and_drain(_ats=_ats, _schema_directory=_schema_directory, _test_directory=_test_directory)
    check_storage_and_plugin_message(_ats=_ats, _schema_directory=_schema_directory, _test_directory=_test_directory)
