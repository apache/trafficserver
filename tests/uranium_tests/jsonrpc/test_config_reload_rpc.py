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

from tools.uranium.services import ATS, ATSFactory


class ConfigReloadRpcScenario:
    """Exercise file and inline modes of admin_config_reload."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._request_id = 0
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable diagnostics for the unified config reload RPC."""

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc|config",
        })
        return ats

    def rpc(self, method: str, params: object | None = None) -> dict[str, Any]:
        """Invoke one method and return its decoded JSON-RPC response."""

        self._request_id += 1
        request: dict[str, object] = {
            "jsonrpc": "2.0",
            "id": str(self._request_id),
            "method": method,
        }
        if params is not None:
            request["params"] = params
        command = self._ats.rpc(request)
        assert command.returncode == 0, command.output
        return json.loads(command.stdout)

    def reload(self, configs: object | None = None) -> dict[str, Any]:
        """Invoke admin_config_reload in file or inline mode."""

        params = None if configs is None else {"configs": configs}
        response = self.rpc("admin_config_reload", params)
        assert "error" not in response, response
        return response["result"]

    @staticmethod
    def assert_result_error(result: dict[str, Any], *codes: str) -> None:
        """Require one expected nested config error code or message."""

        errors = result.get("errors", [])
        assert errors, result
        rendered = str(errors)
        assert any(code in rendered for code in codes), errors

    def check_basic_modes(self) -> None:
        """Verify file reload, empty inline content, and unknown keys."""

        file_result = self.reload()
        assert file_result.get("token") or file_result.get("errors") is not None
        time.sleep(2)
        empty = self.reload({})
        assert empty.get("message") == ["No configs were scheduled for reload"]
        time.sleep(2)
        self.assert_result_error(
            self.reload({"unknown_config_key": {
                "some": "data"
            }}),
            "6010",
            "not registered",
        )
        time.sleep(1)
        self.assert_result_error(
            self.reload({"remap.config": {
                "some": "data"
            }}),
            "6010",
            "not registered",
        )

    def check_file_only_rejections(self) -> None:
        """Verify registered FileOnly handlers reject inline content."""

        time.sleep(2)
        self.assert_result_error(
            self.reload({"ip_allow": [{
                "apply": "in",
                "ip_addrs": "127.0.0.1",
                "action": "allow",
                "methods": ["GET", "HEAD"],
            }]}),
            "6011",
            "does not support RPC",
        )
        time.sleep(2)
        multiple = self.reload(
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
        self.assert_result_error(multiple, "6010", "6011")

    def check_overlapping_reloads(self) -> None:
        """Verify inline work is rejected while or after file reload begins."""

        time.sleep(1)
        first = self.reload()
        assert "token" in first or "errors" in first
        overlapping = self.reload({"ip_allow": [{"apply": "in", "ip_addrs": "10.0.0.0/8"}]})
        self.assert_result_error(overlapping, "6011", "6004")
        time.sleep(3)
        token_case = self.reload({"unknown_for_token_test": {"data": "value"}})
        token = token_case.get("token", "")
        assert not token or token.startswith("inline-")

    def check_structures_and_directives(self) -> None:
        """Pass nested content, large documents, and reload directives."""

        time.sleep(2)
        nested = self.reload(
            {"records": {
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
        status = self.rpc("get_reload_config_status")
        assert "result" in status or "error" in status
        time.sleep(2)
        large_ip_allow = [{"apply": "in", "ip_addrs": f"10.{index}.0.0/16", "action": "allow"} for index in range(50)]
        self.assert_result_error(self.reload({"ip_allow": large_ip_allow}), "6011")
        time.sleep(2)
        self.assert_result_error(
            self.reload({"sni": {
                "_reload": {
                    "fqdn": "*.example.com"
                }
            }}),
            "6011",
        )
        time.sleep(2)
        self.assert_result_error(
            self.reload({"virtualhost": {
                "_reload": {
                    "id": "myhost.example.com"
                }
            }}),
            "6010",
            "not registered",
        )
        time.sleep(2)
        self.assert_result_error(
            self.reload(
                {
                    "ip_allow":
                        {
                            "_reload": {
                                "validate_only": "true"
                            },
                            "rules": [{
                                "apply": "in",
                                "ip_addrs": "0/0",
                                "action": "allow"
                            }],
                        }
                }),
            "6011",
        )

    def run(self) -> None:
        """Run every config reload RPC behavior."""

        self._ats.start()
        self.check_basic_modes()
        self.check_file_only_rejections()
        self.check_overlapping_reloads()
        self.check_structures_and_directives()


def test_config_reload_rpc(ats_factory: ATSFactory) -> None:
    """admin_config_reload handles file, inline, error, and directive modes."""

    ConfigReloadRpcScenario(ats_factory).run()
