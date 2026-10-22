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
"""Regression checks for asynchronous native-test synchronization."""

from importlib import import_module
import json
from types import SimpleNamespace

import pytest


@pytest.mark.parametrize("scenario", ("tls.test_ssl_multicert_partial_reload", "traffic_ctl.test_traffic_ctl_config_reload"))
def test_reload_waits_for_the_root_task(monkeypatch: pytest.MonkeyPatch, scenario: str) -> None:
    """Do not treat successful children as a completed TLS reload.

    :param monkeypatch: Fixture replacing the polling delay.
    :param scenario: Native-test module whose reload synchronization is checked.
    """
    module = import_module(f"uranium_tests.{scenario}")
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)
    statuses = iter(("in_progress", "success"))
    observed = []

    def rpc(request: dict[str, object]) -> SimpleNamespace:
        """Return successive snapshots of one asynchronous reload.

        :param request: JSON-RPC status request from the helper.
        """
        assert request["method"] == "get_reload_config_status"
        status = next(statuses)
        observed.append(status)
        payload = {"result": {"tasks": [{"status": status, "sub_tasks": [{"status": "success"}]}]}}
        return SimpleNamespace(returncode=0, stdout=json.dumps(payload), output="")

    ats = SimpleNamespace(traffic_ctl=lambda *_args: SimpleNamespace(returncode=0, output="", stdout=""), rpc=rpc)
    if scenario.startswith("tls."):
        module.reload(ats, "reload-token", "success")
    else:
        module.wait_for_reload("reload-token", _ats=ats)
    assert observed == ["in_progress", "success"]
