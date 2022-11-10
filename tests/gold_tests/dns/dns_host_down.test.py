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

class DownCachedOriginServerTest:
    replay_file = "replay/server_down.replay.yaml"

    def __init__(self, host_down = False):
        """Initialize the Test processes for the test runs."""
        self._dns_port = None

    def _configure_trafficserver(self):
        """Configure Traffic Server."""
        self._ts = Test.MakeATSProcess("ts", enable_cache=False)

        self._ts.Disk.remap_config.AddLine(
            f"map / http://resolve.this.com:{self._server.Variables.http_port}/"
        )

        self._ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 0,
            'proxy.config.diags.debug.tags': 'hostdb|dns|http|socket',
            'proxy.config.http.connect_attempts_max_retries': 0,
            'proxy.config.http.connect_attempts_rr_retries': 0,
            'proxy.config.hostdb.fail.timeout': 10,
            'proxy.config.dns.resolv_conf': 'NULL',
            'proxy.config.hostdb.ttl_mode': 1,
            'proxy.config.hostdb.timeout': 10,
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.http.transaction_no_activity_timeout_in': 2,
            'proxy.config.hostdb.host_file.interval' : 1,
            'proxy.config.hostdb.host_file.path': os.path.join(Test.TestDirectory, "hosts_file"),
        })

    # Even when the origin server is down, SM will return a hit-fresh domain from HostDB.
    # After request has failed, SM should mark the IP as down
    def _test_host_mark_down(self):
        tr = Test.AddTestRun()
        self._server = tr.AddVerifierServerProcess("server", DownCachedOriginServerTest.replay_file)
        self._configure_trafficserver()

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.DelayStart = 1
        tr.AddVerifierClientProcess(
            "client-1",
            DownCachedOriginServerTest.replay_file,
            http_ports=[self._ts.Variables.port],
            other_args='--keys 1')

        tr.NotRunningAfter = self._server

    # After host has been marked down from previous test, HostDB should not return
    # the host as available and DNS lookup should fail.
    def _test_host_unreachable(self):
        tr = Test.AddTestRun()

        tr.NotRunningBefore = self._server

        tr.DelayStart = 1
        tr.AddVerifierClientProcess(
            "client-2",
            DownCachedOriginServerTest.replay_file,
            http_ports=[self._ts.Variables.port],
            other_args='--keys 2')

    def run(self):
        self._test_host_mark_down()
        self._test_host_unreachable()

DownCachedOriginServerTest().run()
