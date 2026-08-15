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


class QuicPollTimeoutScenario:
    """Start a QUIC listener and verify its configured UDP poll timeout."""

    def __init__(self, ats_factory: ATSFactory, configured_timeout: int | None) -> None:
        self._configured_timeout = configured_timeout
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Enable QUIC debug output and optionally override the timeout."""

        ats = ats_factory.create("ts", enable_quic=True, enable_tls=True)
        if not ats.has_feature("TS_HAS_QUICHE"):
            pytest.skip("ATS with QUICHE is required")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "net|v_quic|quic|socket|inactivity_cop|v_iocore_net_poll",
            })
        if self._configured_timeout is not None:
            ats.records.update({"proxy.config.udp.poll_timeout": self._configured_timeout})
        return ats

    def run(self) -> None:
        """Start ATS and require the effective timeout in traffic.out."""

        expected = 100 if self._configured_timeout is None else self._configured_timeout
        self._ats.start()
        wait_for_file_lines(self._ats.traffic_out, rf"ET_UDP.*timeout: {expected},", 1)


@pytest.mark.parametrize("configured_timeout", (None, 10), ids=("default", "override"))
def test_quic_poll_timeout(ats_factory: ATSFactory, configured_timeout: int | None) -> None:
    """The QUIC poller uses the default or explicitly configured timeout."""

    QuicPollTimeoutScenario(ats_factory, configured_timeout).run()
