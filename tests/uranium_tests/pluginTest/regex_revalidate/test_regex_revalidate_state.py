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
import re
import time

import pytest

from tools.uranium.services import ATS, ATSFactory, OriginServer, ServiceFactory, wait_for_file_lines


def configure_origin(services: ServiceFactory) -> OriginServer:
    """Create the origin referenced by remap.config.

    :param services: Factory owning support services and their cleanup.
    """

    origin = services.origin("server")
    origin.add_response(
        {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n"},
        {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=300\r\n\r\n",
            "body": "xxx",
        },
    )
    return origin


def configure_ats(
        ats_factory: ATSFactory, *, _origin: OriginServer, _path0_expiry: int, _path1_epoch: int, _path1_expiry: int) -> ATS:
    """Configure the plugin with both rules and the initial state.

    :param _origin: Test-local origin configured by the test.
    :param _path0_expiry: Test-local path0 expiry configured by the test.
    :param _path1_epoch: Test-local path1 epoch configured by the test.
    :param _path1_expiry: Test-local path1 expiry configured by the test.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts")
    if not ats.plugin_exists("regex_revalidate.so"):
        pytest.skip("regex_revalidate.so is not installed")
    state_path = ats.runtime_directory / "reval.state"
    ats.plugin_config.add_line(f"regex_revalidate.so -d -c reval.conf -l reval.log -f {state_path}")
    ats.write_config_file(
        "reval.conf",
        f"path0 {_path0_expiry} STALE\n"
        f"path1 {_path1_expiry} MISS\n",
    )
    ats.write_runtime_file(
        "reval.state",
        f"path1 {_path1_epoch} {_path1_expiry} MISS\n"
        f"dummy {_path1_epoch} {_path1_expiry} MISS\n",
    )
    ats.remap_config.add_line(f"map http://ats/ http://127.0.0.1:{_origin.port}")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "regex_revalidate",
            "proxy.config.http.wait_for_cache": 1,
        })
    return ats


def check_merged_state(*, _path0_expiry: int, _path1_epoch: int, _path1_expiry: int, _state_path: Path) -> None:
    """Verify only configured rules survive and their epoch data merges.

    :param _path0_expiry: Test-local path0 expiry configured by the test.
    :param _path1_epoch: Test-local path1 epoch configured by the test.
    :param _path1_expiry: Test-local path1 expiry configured by the test.
    :param _state_path: Test-local state path configured by the test.
    """

    content = wait_for_file_lines(_state_path, r"^path0 ", 1)
    lines = content.splitlines()
    assert len(lines) == 2, content
    assert re.fullmatch(rf"path0 \d+ {_path0_expiry} STALE", lines[0]), content
    assert lines[1] == f"path1 {_path1_epoch} {_path1_expiry} MISS"


def test_regex_revalidate_state(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """regex_revalidate merges persisted epochs for matching startup rules.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    _origin = configure_origin(services)
    _path0_expiry = int(time.time()) + 90
    _path1_epoch = int(time.time()) - 50
    _path1_expiry = int(time.time()) + 600
    _ats = configure_ats(
        ats_factory, _origin=_origin, _path0_expiry=_path0_expiry, _path1_epoch=_path1_epoch, _path1_expiry=_path1_expiry)
    _state_path = _ats.runtime_directory / "reval.state"

    _origin.start()
    _ats.start()
    check_merged_state(_path0_expiry=_path0_expiry, _path1_epoch=_path1_epoch, _path1_expiry=_path1_expiry, _state_path=_state_path)
