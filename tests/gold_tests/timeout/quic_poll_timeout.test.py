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

from typing import Optional

Test.Summary = 'Checks ts.proxy.config.udp.poll_timeout'

Test.SkipUnless(Condition.HasATSFeature('TS_HAS_QUICHE'))


class TestPollTimeout:
    """Configure a test for poll_timeout."""

    ts_counter: int = 0

    def __init__(self, name: str, udp_poll_timeout_in: Optional[int] = None) -> None:
        """Initialize the test.

        :param name: The name of the test.
        :param udp_poll_timeout_in: Configuration value for proxy.config.udp.poll_timeout
        """
        self.name = name
        self.udp_poll_timeout_in = udp_poll_timeout_in
        self.expected_udp_poll_timeout = 100
        if udp_poll_timeout_in is not None:
            self.expected_udp_poll_timeout = udp_poll_timeout_in

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(f"ts-{TestPollTimeout.ts_counter}", enable_quic=True, enable_tls=True)

        TestPollTimeout.ts_counter += 1
        self._ts = ts
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'net|v_quic|quic|socket|inactivity_cop|v_iocore_net_poll',
            })

        if self.udp_poll_timeout_in is not None:
            self._ts.Disk.records_config.update({'proxy.config.udp.poll_timeout': self.udp_poll_timeout_in})

    def run(self):
        """Run the test."""
        tr = Test.AddTestRun(self.name)
        self._configure_traffic_server(tr)

        tr.Processes.Default.Command = "echo 'testing ts.proxy.config.udp.poll_timeout'"
        tr.Processes.Default.StartBefore(self._ts)

        self._ts.Disk.traffic_out.Content += Testers.IncludesExpression(
            f"ET_UDP.*timeout: {self.expected_udp_poll_timeout},", "Verify UDP poll timeout.")


# Tests start.

test0 = TestPollTimeout("Test ts.proxy.config.udp.poll_timeout with default value.")
test0.run()

test1 = TestPollTimeout("Test ts.proxy.config.udp.poll_timeout with value of 10.", udp_poll_timeout_in=10)
test1.run()
