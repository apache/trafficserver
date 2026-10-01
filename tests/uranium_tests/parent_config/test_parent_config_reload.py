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
    """Create ATS with one observable parent-selection rule.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "parent_select|config",
    })
    ats.parent_config.add_line('dest_domain=example.com parent="origin.example.com:80"')
    return ats


def wait_for_loads(expected: int, *, _ats: ATS) -> None:
    """Wait for @a expected completed parent.config loads.

    :param _ats: Test-local ats configured by the test.
    :param expected: Expected result for this case.
    """

    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if _ats.diags_log.read_text(errors="replace").count("parent.config finished loading") >= expected:
            return
        time.sleep(0.1)
    raise AssertionError(f"parent.config did not finish loading {expected} times")


def reload_touched_file(*, _ats: ATS) -> None:
    """Touch parent.config and request a normal configuration reload.

    :param _ats: Test-local ats configured by the test.
    """

    _ats.parent_config.path.touch()
    result = _ats.traffic_ctl("config", "reload")
    assert result.returncode == 0, result.output
    wait_for_loads(2, _ats=_ats)


def reload_after_record_update(*, _ats: ATS) -> None:
    """Verify the registered retry-time callback reloads parent.config.

    :param _ats: Test-local ats configured by the test.
    """

    result = _ats.traffic_ctl("config", "set", "proxy.config.http.parent_proxy.retry_time", "60")
    assert result.returncode == 0, result.output
    wait_for_loads(3, _ats=_ats)


def test_parent_config_reload(ats_factory: ATSFactory) -> None:
    """parent.config reloads for file and record changes.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """
    _ats = configure_ats(ats_factory)

    _ats.start()
    wait_for_loads(1, _ats=_ats)
    reload_touched_file(_ats=_ats)
    reload_after_record_update(_ats=_ats)
