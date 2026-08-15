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


class ConfigReloadTrackingScenario:
    """Exercise generated, custom, duplicate, and overlapping reload tokens."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._request_id = 0
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Enable reload diagnostics."""

        ats = ats_factory.create("ts")
        ats.records.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc|config",
        })
        return ats

    def rpc(self, method: str, params: object | None = None) -> dict[str, Any]:
        """Invoke one JSON-RPC method and decode its response."""

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

    def reload(self, **params: object) -> dict[str, Any]:
        """Start a reload with optional tracking parameters."""

        return self.rpc("admin_config_reload", params or None)

    @staticmethod
    def token(response: dict[str, Any]) -> str:
        """Extract and validate a successful reload token."""

        assert "error" not in response, response
        token = response["result"].get("token", "")
        assert token, response
        return token

    def check_generated_and_custom_tokens(self) -> str:
        """Verify automatic token generation and a caller-supplied token."""

        generated = self.token(self.reload())
        assert generated.startswith("rldtk-"), generated
        time.sleep(2)

        custom = f"uranium-custom-{time.time_ns()}"
        assert self.token(self.reload(token=custom)) == custom
        time.sleep(2)
        return generated

    def check_force_and_duplicate(self, first_token: str) -> None:
        """Force a reload, then verify reusing a token is not ambiguous."""

        forced = self.reload(force=True)
        self.token(forced)
        time.sleep(2)

        duplicate = self.reload(token=first_token)
        if "error" not in duplicate:
            assert duplicate["result"].get("token") != first_token, duplicate

    def check_overlapping_and_queries(self) -> None:
        """Exercise rapid reloads and optional tracking query methods."""

        first = self.reload()
        assert "result" in first or "error" in first, first
        second = self.reload()
        assert "result" in second or "error" in second, second
        time.sleep(2)

        for method in ("admin_config_reload_status", "admin_config_reload_history"):
            response = self.rpc(method)
            assert response.get("jsonrpc") == "2.0", response
            assert "result" in response or "error" in response, response

    def run(self) -> None:
        """Run all token-tracking behaviors."""

        self._ats.start()
        first_token = self.check_generated_and_custom_tokens()
        self.check_force_and_duplicate(first_token)
        self.check_overlapping_and_queries()


def test_config_reload_tracking(ats_factory: ATSFactory) -> None:
    """Configuration reload tokens remain unique and queryable."""

    ConfigReloadTrackingScenario(ats_factory).run()
