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

import time

from tools.uranium.services import ATS, ATSFactory


def configure_ats(ats_factory: ATSFactory) -> ATS:
    """Configure every reload handler involved in the race.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_cache=True)
    ats.set_startup_timeout(30)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "rpc|config|config.reload|filemanager",
        })
    ats.write_config_file(
        "ip_allow.yaml",
        "ip_allow:\n"
        "  - apply: in\n"
        "    ip_addrs: 0/0\n"
        "    action: allow\n"
        "    methods: ALL\n",
    )
    ats.set_logging_yaml({"logging": {
        "formats": [{
            "name": "reserve_test",
            "format": "%<cqtq>",
        }]
    }})
    ats.write_config_file(
        "sni.yaml",
        'sni:\n  - fqdn: "*.example.com"\n    verify_client: NONE\n',
    )
    return ats


def change_records_without_notifying_ats(*, _ats: ATS) -> None:
    """Change a trigger record on disk before the explicit reload.

    :param _ats: Test-local ats configured by the test.
    """

    result = _ats.traffic_ctl(
        "config",
        "set",
        "proxy.config.diags.debug.tags",
        "rpc|config|config.reload|filemanager|upd",
        "--cold",
    )
    assert result.returncode == 0, result.output


def touch_other_reload_files(*, _ats: ATS) -> None:
    """Make file-based handlers participate in the same reload.

    :param _ats: Test-local ats configured by the test.
    """

    paths = (
        _ats.config_directory / "ip_allow.yaml",
        _ats.config_directory / "logging.yaml",
        _ats.config_directory / "sni.yaml",
        _ats.config_directory / "cache.config",
    )
    for path in paths:
        path.touch()


def reload_and_check_status(*, _ats: ATS) -> None:
    """Trigger a named reload and verify its terminal state.

    :param _ats: Test-local ats configured by the test.
    """

    result = _ats.traffic_ctl("config", "reload", "-t", "reserve_subtask_test")
    assert result.returncode == 0, result.output
    time.sleep(15)
    status = _ats.traffic_ctl("config", "status", "-t", "reserve_subtask_test")
    assert status.returncode == 0, status.output
    assert "success" in status.stdout
    assert "in_progress" not in status.stdout


def check_reload_diagnostics(*, _ats: ATS) -> None:
    """Verify reservation succeeded without conflicting transitions.

    :param _ats: Test-local ats configured by the test.
    """

    traffic_out = _ats.traffic_out.read_text(errors="replace")
    assert "Reserved subtask" in traffic_out
    assert "ignoring transition from" not in traffic_out


def test_config_reload_reserve_subtask(ats_factory: ATSFactory) -> None:
    """Reload handlers can reserve children after the parent first succeeds.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    time.sleep(3)
    change_records_without_notifying_ats(_ats=_ats)
    touch_other_reload_files(_ats=_ats)
    reload_and_check_status(_ats=_ats)
    check_reload_diagnostics(_ats=_ats)
