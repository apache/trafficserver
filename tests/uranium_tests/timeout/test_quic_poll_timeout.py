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

import pytest

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines


def configure_ats(ats_factory: ATSFactory, *, _configured_timeout: int | None) -> ATS:
    """Enable QUIC debug output and optionally override the timeout.

    :param _configured_timeout: UDP polling timeout in milliseconds, or None for the default.
    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ats = ats_factory.create("ts", enable_quic=True, enable_tls=True)
    if not ats.has_feature("TS_HAS_QUICHE"):
        pytest.skip("ATS with QUICHE is required")
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "net|v_quic|quic|socket|inactivity_cop|v_iocore_net_poll",
        })
    if _configured_timeout is not None:
        ats.records.update({"proxy.config.udp.poll_timeout": _configured_timeout})
    return ats


@pytest.mark.parametrize("configured_timeout", (None, 10), ids=("default", "override"))
def test_quic_poll_timeout(ats_factory: ATSFactory, configured_timeout: int | None) -> None:
    """The QUIC poller uses the default or explicitly configured timeout.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param configured_timeout: UDP polling timeout in milliseconds, or None for the default.
    """
    _ats = configure_ats(ats_factory, _configured_timeout=configured_timeout)

    expected = 100 if configured_timeout is None else configured_timeout
    _ats.start()
    wait_for_file_lines(_ats.traffic_out, rf"ET_UDP.*timeout: {expected},", 1)
