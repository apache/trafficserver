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
import time

import pytest

from tools.uranium.services import ATS, ATSFactory
from .config_reload_helpers import rpc


def test_config_reload_rpc(ats_factory: ATSFactory) -> None:
    """admin_config_reload handles file, inline, error, and directive modes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Enable diagnostics for the unified config reload RPC.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc|config",
        })
        ats.write_config_file("virtualhost.yaml", "virtualhost:\n  - id: myhost.example.com\n    domains: [myhost.example.com]\n")
        return ats

    def rpc(method: str, params: object | None = None) -> dict[str, Any]:
        """Invoke one method and return its decoded JSON-RPC response.

        :param method: Method used by this test step.
        :param params: Params used by this test step.
        """
        nonlocal _request_id

        _request_id += 1
        request: dict[str, object] = {
            "jsonrpc": "2.0",
            "id": str(_request_id),
            "method": method,
        }
        if params is not None:
            request["params"] = params
        command = _ats.rpc(request)
        assert command.returncode == 0, command.output
        return json.loads(command.stdout)

    def reload(configs: object | None = None) -> dict[str, Any]:
        """Invoke admin_config_reload in file or inline mode.

        :param configs: Configs used by this test step.
        """

        params = None if configs is None else {"configs": configs}
        response = rpc("admin_config_reload", params)
        assert "error" not in response, response
        return response["result"]

    def assert_result_error(result: dict[str, Any], *codes: str) -> None:
        """Require one expected nested config error code or message.

        :param result: Completed command result to validate.
        :param codes: Codes used by this test step.
        """

        errors = result.get("errors", [])
        assert errors, result
        rendered = str(errors)
        assert any(code in rendered for code in codes), errors

    _request_id = 0
    _ats = configure_ats(ats_factory)

    _ats.start()

    file_result = reload()
    assert file_result.get("token") or file_result.get("errors") is not None
    time.sleep(2)
    empty = reload({})
    assert empty.get("message") == ["No configs were scheduled for reload"]
    time.sleep(2)
    assert_result_error(
        reload({"unknown_config_key": {
            "some": "data"
        }}),
        "6010",
        "not registered",
    )
    time.sleep(1)
    assert_result_error(
        reload({"remap.config": {
            "some": "data"
        }}),
        "6010",
        "not registered",
    )

    time.sleep(2)
    assert_result_error(
        reload({"ip_allow": [{
            "apply": "in",
            "ip_addrs": "127.0.0.1",
            "action": "allow",
            "methods": ["GET", "HEAD"],
        }]}),
        "6011",
        "does not support RPC",
    )
    time.sleep(2)
    multiple = reload(
        {
            "ip_allow": [{
                "apply": "in",
                "ip_addrs": "0.0.0.0/0",
                "action": "allow"
            }],
            "sni": [{
                "fqdn": "*.test.com",
                "verify_client": "NONE"
            }],
            "records": {
                "diags": {
                    "debug": {
                        "enabled": 1
                    }
                }
            },
        })
    assert_result_error(multiple, "6010", "6011")

    time.sleep(1)
    first = reload()
    assert "token" in first or "errors" in first
    overlapping = reload({"ip_allow": [{"apply": "in", "ip_addrs": "10.0.0.0/8"}]})
    assert_result_error(overlapping, "6011", "6004")
    time.sleep(3)
    token_case = reload({"unknown_for_token_test": {"data": "value"}})
    token = token_case.get("token", "")
    assert not token or token.startswith("inline-")

    time.sleep(2)
    nested = reload({"records": {
        "diags": {
            "debug": {
                "enabled": 1,
                "tags": "http|rpc|test"
            }
        },
        "http": {
            "cache": {
                "http": 1
            }
        },
    }})
    assert isinstance(nested, dict)
    time.sleep(2)
    status = rpc("get_reload_config_status")
    assert "result" in status or "error" in status
    time.sleep(2)
    large_ip_allow = [{"apply": "in", "ip_addrs": f"10.{index}.0.0/16", "action": "allow"} for index in range(50)]
    assert_result_error(reload({"ip_allow": large_ip_allow}), "6011")
    time.sleep(2)
    assert_result_error(
        reload({"sni": {
            "_reload": {
                "fqdn": "*.example.com"
            }
        }}),
        "6011",
    )
    time.sleep(2)
    virtualhost = reload({"virtualhost": {"_reload": {"id": "myhost.example.com"}}})
    assert not virtualhost.get("errors"), virtualhost
    assert virtualhost.get("tasks") or virtualhost.get("message"), virtualhost
    time.sleep(2)
    assert_result_error(
        reload(
            {"ip_allow": {
                "_reload": {
                    "validate_only": "true"
                },
                "rules": [{
                    "apply": "in",
                    "ip_addrs": "0/0",
                    "action": "allow"
                }],
            }}),
        "6011",
    )


@pytest.mark.parametrize(
    "content,config,marker", [
        ("present", [{
            "id": "myhost.example.com"
        }], "virtualhost does not accept config content over rpc"),
        ("present", {
            "_reload": {
                "id": "absent.example.com"
            }
        }, "virtualhost with id 'absent.example.com' not found"),
        ("missing", {
            "_reload": {
                "id": "myhost.example.com"
            }
        }, "Cannot reload virtualhost entry 'myhost.example.com'"),
        ("present", {
            "_reload": {
                "id": ""
            }
        }, "must name an entry"),
        ("missing", {
            "_reload": {}
        }, "Cannot reload virtualhost config"),
        ("present", {
            "_reload": {
                "ID": "myhost.example.com"
            }
        }, "directive 'ID' is not supported"),
    ])
def test_virtualhost_reload_failure(ats: ATS, content: str, config: object, marker: str) -> None:
    """Report virtualhost handler failures as failed asynchronous tasks.

    :param ats: Isolated server exposing the reload RPC.
    :param content: Whether virtualhost.yaml exists.
    :param config: Inline virtualhost reload payload.
    :param marker: Required handler diagnostic in the failed task.
    """
    if content == "present":
        ats.write_config_file("virtualhost.yaml", "virtualhost:\n  - id: myhost.example.com\n    domains: [myhost.example.com]\n")
    else:
        ats.omit_config_file("virtualhost.yaml")
    ats.start()
    response = rpc(ats, "admin_config_reload", {"configs": {"virtualhost": config}})
    assert "error" not in response, response
    result = response["result"]
    assert not result.get("errors"), result
    token = result["token"]
    deadline = time.monotonic() + 20
    status = {}
    while time.monotonic() < deadline:
        status = rpc(ats, "get_reload_config_status", {"token": token})
        tasks = list(status.get("result", {}).get("tasks", []))
        matched = False
        while tasks:
            task = tasks.pop()
            if task.get("status") == "fail" and any(marker in entry.get("text", "") for entry in task.get("logs", [])):
                matched = True
            tasks.extend(task.get("sub_tasks", []))
        if matched:
            break
        time.sleep(0.1)
    else:
        pytest.fail(f"Reload did not report the expected failed task: {status}")
    assert ats.is_running
