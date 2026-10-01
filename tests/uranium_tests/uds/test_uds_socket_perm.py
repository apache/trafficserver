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
import stat

from tools.uranium.services import ATS, ATSFactory


def configure_ats(name: str, permission: str | None, *, _ats_factory: ATSFactory) -> ATS:
    """Create an ATS instance with one TCP and one UDS listener.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param name: Unique service or case name within this test.
    :param permission: Permission used by this test step.
    """

    ats = _ats_factory.create(name)
    uds_listener = ats.uds_path if permission is None else f"{ats.uds_path}:uds-perm={permission}"
    ats.records.update({"proxy.config.http.server_ports": f"{ats.http_port} {uds_listener}"})
    return ats


def assert_mode(ats: ATS, expected: int) -> None:
    """Assert the materialized socket has exactly @a expected permissions.

    :param ats: Traffic Server instance configured or queried by this step.
    :param expected: Expected result for this case.
    """

    actual = stat.S_IMODE(Path(ats.uds_path).stat().st_mode)
    assert actual == expected, f"{ats.uds_path} mode is {actual:#o}, expected {expected:#o}"


def test_uds_socket_perm(ats_factory: ATSFactory) -> None:
    """UDS listeners default to 0666 and honor uds-perm.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    default = configure_ats("ts-default", None, _ats_factory=ats_factory)
    default.start()
    assert_mode(default, 0o666)

    custom = configure_ats("ts-custom", "0660", _ats_factory=ats_factory)
    custom.start()
    assert_mode(custom, 0o660)
