'''
Verify proxy.config.http.connect.down.policy=3 marks the origin down on
transaction inactive timeout (server goes silent after connection is established),
and that policy=2 does not mark the origin down for the same scenario.
'''
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

import os

Test.Summary = '''
Verify connect.down.policy=3 marks the origin down on inactive timeout after
connection is established, and policy=2 does not.
'''

REPLAY_FILE = "replay/inactive_timeout.replay.yaml"

# Inactivity timeout in seconds. The replay server-response delay (10s) is
# intentionally longer so ATS fires the timeout before the server replies.
INACTIVITY_TIMEOUT = 3


class ConnectDownPolicy3Test:
    """
    Test that policy=3 marks the origin server down when the server goes silent
    and ATS fires an INACTIVE_TIMEOUT.

    Sequence:
      1. ATS connects to the origin and sends the request.
      2. The origin delays its response beyond INACTIVITY_TIMEOUT.
      3. ATS fires VC_EVENT_INACTIVITY_TIMEOUT, calling track_connect_fail().
      4. Under policy=3 track_connect_fail() returns true → mark_host_failure().
      5. With connect_attempts_rr_retries=1, one failure is enough to mark the
         host down, which writes a "marking down" entry to error.log.
    """

    def __init__(self, policy, expect_mark_down):
        self._policy = policy
        self._expect_mark_down = expect_mark_down
        self._name = f"policy{policy}"
        self._server = Test.MakeVerifierServerProcess(f"server-{self._name}", REPLAY_FILE)
        self._configure_trafficserver()

    def _configure_trafficserver(self):
        self._ts = Test.MakeATSProcess(f"ts-{self._name}", enable_cache=False)

        self._ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.http_port}/")

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'hostdb|http',
                # Use policy under test.
                'proxy.config.http.connect.down.policy': self._policy,
                # No retries so the timeout triggers mark-down immediately.
                'proxy.config.http.connect_attempts_max_retries': 0,
                # One failure is enough to mark the host down and write to error.log.
                'proxy.config.http.connect_attempts_rr_retries': 1,
                # Short server-side inactivity timeout so the test runs quickly.
                'proxy.config.http.transaction_no_activity_timeout_out': INACTIVITY_TIMEOUT,
                # Keep the host marked down long enough to verify.
                'proxy.config.hostdb.fail.timeout': 60,
            })

    def _test_inactive_timeout(self):
        tr = Test.AddTestRun(f"policy={self._policy}: inactive timeout triggers 504")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.AddVerifierClientProcess(f"client-{self._name}", REPLAY_FILE, http_ports=[self._ts.Variables.port])

    def _test_mark_down(self):
        if self._expect_mark_down:
            # Wait for error.log to appear then verify it contains the mark-down entry.
            tr = Test.AddTestRun(f"policy={self._policy}: check error.log for mark-down")
            tr.Processes.Default.Command = (
                os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
                os.path.join(self._ts.Variables.LOGDIR, 'error.log'))
            self._ts.Disk.error_log.Content = Testers.ContainsExpression(
                "marking down", f"policy={self._policy}: origin should be marked down after inactive timeout")
        else:
            # For policy=2 the host should not be marked down, so error.log should
            # not exist. Verify by checking traffic.out has no mark-down message.
            tr = Test.AddTestRun(f"policy={self._policy}: verify no mark-down")
            tr.Processes.Default.Command = "true"
            self._ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
                "marking down", f"policy={self._policy}: origin should NOT be marked down after inactive timeout")

    def run(self):
        self._test_inactive_timeout()
        self._test_mark_down()


# Policy 3: inactive timeout SHOULD mark the origin down.
ConnectDownPolicy3Test(policy=3, expect_mark_down=True).run()

# Policy 2: inactive timeout should NOT mark the origin down.
ConnectDownPolicy3Test(policy=2, expect_mark_down=False).run()
