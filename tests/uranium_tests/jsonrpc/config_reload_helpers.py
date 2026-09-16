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
"""Shared operations for native configuration-reload scenarios."""

from typing import Any
import json
import time

from tools.uranium.services import ATS, CommandResult


def rpc(ats: ATS, method: str, params: object | None = None, *, request_id: str = "1") -> dict[str, Any]:
    """Invoke one JSON-RPC method and decode its response.

    :param ats: Running Traffic Server instance.
    :param method: JSON-RPC method name.
    :param params: Optional method parameters.
    :param request_id: JSON-RPC request identifier.
    """

    request: dict[str, object] = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        request["params"] = params
    result = ats.rpc(request)
    assert result.returncode == 0, result.output
    return json.loads(result.stdout)


def require_command(result: CommandResult, *contains: str, excludes: tuple[str, ...] = ()) -> str:
    """Require a successful command and its expected output fragments.

    :param result: Completed command result.
    :param contains: Text fragments that must appear in combined output.
    :param excludes: Text fragments forbidden in combined output.
    """

    assert result.returncode == 0, result.output
    for fragment in contains:
        assert fragment in result.output, result.output
    for fragment in excludes:
        assert fragment not in result.output, result.output
    return result.output


def wait_for_status(
        ats: ATS,
        token: str,
        *contains: str,
        excludes: tuple[str, ...] = (),
        extra_arguments: tuple[str, ...] = (),
        timeout: float = 20,
) -> str:
    """Wait for one reload status to satisfy an output contract.

    :param ats: Running Traffic Server instance.
    :param token: Reload token supplied to ``traffic_ctl``.
    :param contains: Text fragments that must all appear.
    :param excludes: Text fragments forbidden from the final output.
    :param extra_arguments: Additional ``config status`` arguments.
    :param timeout: Maximum number of seconds to wait.
    """

    deadline = time.monotonic() + timeout
    output = ""
    while time.monotonic() < deadline:
        result = ats.traffic_ctl("config", "status", "-t", token, *extra_arguments)
        output = result.output
        if result.returncode == 0 and all(fragment in output for fragment in contains) and all(
                fragment not in output for fragment in excludes):
            return output
        time.sleep(0.2)
    raise AssertionError(f"Reload {token!r} did not reach the expected status:\n{output}")
