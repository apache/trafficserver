"""
Test child proxy serving stale content when parents are exhausted
"""
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

Test.testName = "proxy_serve_stale"
Test.ContinueOnFail = True


class ProxyServeStaleTest:
    """Verify that stale content is served when the parent is down."""

    single_transaction_replay = "replay/proxy_serve_stale.replay.yaml"
    ts_parent_hostname = "localhost:82"

    def __init__(self):
        """Initialize the test."""
        self._configure_server()
        self._configure_ts()

    def _configure_server(self):
        self.server = Test.MakeVerifierServerProcess("server", self.single_transaction_replay)
        self.nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

    def _configure_ts(self):
        self.ts_child = Test.MakeATSProcess("ts_child")
        # Config child proxy to route to parent proxy
        self.ts_child.Disk.records_config.update(
            {
                'proxy.config.http.push_method_enabled': 1,
                'proxy.config.http.parent_proxy.fail_threshold': 2,
                'proxy.config.http.parent_proxy.total_connect_attempts': 1,
                'proxy.config.http.cache.max_stale_age': 10,
                'proxy.config.http.parent_proxy.self_detect': 0,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|dns|parent_proxy',
                'proxy.config.dns.nameservers': f"127.0.0.1:{self.nameserver.Variables.Port}",
            })
        self.ts_child.Disk.parent_config.AddLine(
            f'dest_domain=. parent="{self.ts_parent_hostname}" round_robin=consistent_hash go_direct=false')
        self.ts_child.Disk.remap_config.AddLine(f'map / http://localhost:{self.server.Variables.http_port}')

    def run(self):
        """Run the test cases."""

        tr = Test.AddTestRun()
        tr.AddVerifierClientProcess('client', self.single_transaction_replay, http_ports=[self.ts_child.Variables.port])
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.ts_child
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.nameserver)
        tr.Processes.Default.StartBefore(self.ts_child)


ProxyServeStaleTest().run()
