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


class ConfigReloadFailureScenario:
    """Exercise failure reporting and recovery for configuration reloads."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        self._request_id = 0
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure TLS so a broken certificate reference can fail reload."""

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "config|ssl|ip_allow",
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            })
        self.write_multicert(ats, valid=True)
        return ats

    @staticmethod
    def write_multicert(ats: ATS, *, valid: bool) -> None:
        """Write either the valid baseline or an additional bad certificate."""

        entries = [
            '  - dest_ip: "*"',
            "    ssl_cert_name: server.pem",
            "    ssl_key_name: server.key",
        ]
        if not valid:
            entries.extend(
                [
                    "  - dest_ip: 1.2.3.4",
                    "    ssl_cert_name: /nonexistent/bad.pem",
                    "    ssl_key_name: /nonexistent/bad.key",
                ])
        ats.write_config_file("ssl_multicert.yaml", "ssl_multicert:\n" + "\n".join(entries) + "\n")

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

    def reload(self, *, force: bool = False) -> dict[str, Any]:
        """Start a file-based reload and require a well-formed response."""

        params = {"force": True} if force else None
        response = self.rpc("admin_config_reload", params)
        assert response.get("jsonrpc") == "2.0", response
        assert "result" in response or "error" in response, response
        return response

    def verify_baseline(self) -> None:
        """Require a normal reload to start successfully."""

        response = self.reload(force=True)
        assert "error" not in response, response
        assert response["result"].get("token"), response
        time.sleep(3)

    def verify_failed_subtask(self) -> None:
        """Require a file-only handler to report an inline reload failure."""

        response = self.rpc(
            "admin_config_reload",
            {
                "configs":
                    {
                        "ssl_multicert":
                            {
                                "ssl_multicert":
                                    [
                                        {
                                            "dest_ip": "*",
                                            "ssl_cert_name": "/nonexistent/bad.pem",
                                            "ssl_key_name": "/nonexistent/bad.key",
                                        }
                                    ]
                            }
                    }
            },
        )
        assert "error" not in response, response
        errors = response["result"].get("errors", [])
        assert errors, response
        assert "6011" in str(errors), errors

    def verify_recovery(self) -> None:
        """Restore the valid file and require a new reload to be accepted."""

        time.sleep(2)
        response = self.reload(force=True)
        assert "error" not in response, response
        assert response["result"].get("token"), response

    def run(self) -> None:
        """Run the baseline, failed-subtask, and recovery sequence."""

        self._ats.start()
        self.verify_baseline()
        self.verify_failed_subtask()
        self.verify_recovery()


def test_config_reload_failures(ats_factory: ATSFactory) -> None:
    """A failed reload subtask is reported and does not prevent recovery."""

    ConfigReloadFailureScenario(ats_factory).run()
