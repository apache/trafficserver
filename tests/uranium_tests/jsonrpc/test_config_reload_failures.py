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


def test_config_reload_failures(ats_factory: ATSFactory) -> None:
    """A failed reload subtask is reported and does not prevent recovery.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure TLS so a broken certificate reference can fail reload.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "config|ssl|ip_allow",
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
            })
        write_multicert(ats, valid=True)
        return ats

    def write_multicert(ats: ATS, *, valid: bool) -> None:
        """Write either the valid baseline or an additional bad certificate.

        :param ats: Traffic Server instance configured or queried by this step.
        :param valid: Valid used by this test step.
        """

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

    def rpc(method: str, params: object | None = None) -> dict[str, Any]:
        """Invoke one JSON-RPC method and decode its response.

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

    def reload(*, force: bool = False) -> dict[str, Any]:
        """Start a file-based reload and require a well-formed response.

        :param force: Force used by this test step.
        """

        params = {"force": True} if force else None
        response = rpc("admin_config_reload", params)
        assert response.get("jsonrpc") == "2.0", response
        assert "result" in response or "error" in response, response
        return response

    _request_id = 0
    _ats = configure_ats(ats_factory)

    _ats.start()

    response = reload(force=True)
    assert "error" not in response, response
    assert response["result"].get("token"), response
    time.sleep(3)

    response = rpc(
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

    time.sleep(2)
    response = reload(force=True)
    assert "error" not in response, response
    assert response["result"].get("token"), response
