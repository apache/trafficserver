'''
Verify that ATS can perform a reverse lookup when necessary
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
Verify ATS can perform a reverse lookup when necessary
'''

#
# This test verifies a fix for a regression that incorrectly marked a hostdb record as failed doing a reverse DNS
# lookup from the hosts file.  If a parent.config exists, then ATS will perform a reverse lookup on remaps that
# have an ip address in the map-to url. While this doesn't fail the initil request, subsequent requests that resolve
# to the same host record will see the failed lookup attempt and fail the request with a host not found.
#
# This test verfiies the correct behavior in case future code chages introduce a similar regression
#


class DNSReverseLookupTest:
    replay_file = "replay/reverse_lookup.replay.yaml"

    def __init__(self):
        self._server = Test.MakeVerifierServerProcess("server", DNSReverseLookupTest.replay_file)
        self._configure_trafficserver()

    def _configure_trafficserver(self):
        self._ts = Test.MakeATSProcess("ts", enable_cache=False)

        # This first rule would trigger the bug
        self._ts.Disk.remap_config.AddLine(f"map /test1 http://127.0.0.1:{self._server.Variables.http_port}/",)
        # This first rule would fail in the presense of the bug, but this test verifies correct behavior
        self._ts.Disk.remap_config.AddLine(f"map /test2 http://localhost:{self._server.Variables.http_port}/",)
        self._ts.Disk.parent_config.AddLine(
            f'dest_domain=. parent=parent_host:{self._ts.Variables.port} round_robin=consistent_hash go_direct=false')
        self._ts.Disk.parent_config.AddLine(
            # this doesn't need to match, just exist so ats will do the reverse lookup
            f'dest_host=other_host scheme=http parent="parent_host:8080"')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'hostdb|dns|http',
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

    def test_rev_dns(self):
        tr = Test.AddTestRun()

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess("client", DNSReverseLookupTest.replay_file, http_ports=[self._ts.Variables.port])

    def run(self):
        self.test_rev_dns()


DNSReverseLookupTest().run()
