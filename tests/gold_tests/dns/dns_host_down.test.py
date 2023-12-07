'''
Verify ATS handles down origin servers with domain cached correctly.
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
from ports import get_port
import os

Test.Summary = '''
Verify ATS handles down origin servers with cached domain correctly.
'''
# This test is dependent on the new HostDB restructure that is not in 9.2.x.
# The production patch associated with this test is still applicable for 9.2.x
# but the autest created to show its functionality relies upon the restructured
# HostDB's relationship with down nameservers, which only applies to 10-Dev.
Test.SkipIf(Condition.true("This test depends on new HostDB restructure"))


class DownCachedOriginServerTest:
    replay_file = "replay/server_down.replay.yaml"

    def __init__(self):
        """Initialize the Test processes for the test runs."""
        self._server = Test.MakeVerifierServerProcess("server", DownCachedOriginServerTest.replay_file)
        self._configure_trafficserver()

    def _configure_trafficserver(self):
        """Configure Traffic Server."""
        self._ts = Test.MakeATSProcess("ts", enable_cache=False)

        self._ts.Disk.remap_config.AddLine(f"map / http://resolve.this.com:{self._server.Variables.http_port}/")

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'hostdb|dns|http|socket',
                'proxy.config.http.connect_attempts_max_retries': 0,
                'proxy.config.http.connect_attempts_rr_retries': 0,
                'proxy.config.hostdb.fail.timeout': 10,
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.hostdb.ttl_mode': 1,
                'proxy.config.hostdb.timeout': 2,
                'proxy.config.hostdb.lookup_timeout': 2,
                'proxy.config.http.transaction_no_activity_timeout_in': 2,
                'proxy.config.http.connect_attempts_timeout': 2,
                'proxy.config.hostdb.host_file.interval': 1,
                'proxy.config.hostdb.host_file.path': os.path.join(Test.TestDirectory, "hosts_file"),
            })

    # Even when the origin server is down, SM will return a hit-fresh domain from HostDB.
    # After request has failed, SM should mark the IP as down
    def _test_host_mark_down(self):
        tr = Test.AddTestRun()

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess(
            "client-1", DownCachedOriginServerTest.replay_file, http_ports=[self._ts.Variables.port], other_args='--keys 1')

    # After host has been marked down from previous test, HostDB should not return
    # the host as available and DNS lookup should fail.
    def _test_host_unreachable(self):
        tr = Test.AddTestRun()

        tr.AddVerifierClientProcess(
            "client-2", DownCachedOriginServerTest.replay_file, http_ports=[self._ts.Variables.port], other_args='--keys 2')

    # Verify error log marking host down exists
    def _test_error_log(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
            os.path.join(self._ts.Variables.LOGDIR, 'error.log'))

        self._ts.Disk.error_log.Content = Testers.ContainsExpression("/dns/mark/down' marking down", "host should be marked down")
        self._ts.Disk.error_log.Content = Testers.ContainsExpression(
            "DNS Error: no valid server http://resolve.this.com", "DNS lookup should fail")

    def run(self):
        self._test_host_mark_down()
        self._test_host_unreachable()
        self._test_error_log()


DownCachedOriginServerTest().run()
