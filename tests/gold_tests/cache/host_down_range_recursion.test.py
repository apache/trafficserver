'''
Verify that a Range request to a DOWN host backed by a cache HIT does not
trigger unbounded recursion.
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

from jsonrpc import Request

Test.Summary = '''
Verify that a Range request the cache cannot satisfy against a host marked
DOWN does not trigger unbounded recursion in HttpTransact.
'''


class HostDownRangeRecursionTest:
    prime_replay = "replay/host_down_range_recursion_prime.replay.yaml"
    range_replay = "replay/host_down_range_recursion_range.replay.yaml"

    def __init__(self):
        # Same server process serves both replay files. Only the prime phase
        # actually reaches the server; the range phase is expected to be
        # short-circuited by the DOWN host check.
        self._server = Test.MakeVerifierServerProcess("server", self.prime_replay)
        self._configure_ts()

    def _configure_ts(self):
        self._ts = Test.MakeATSProcess("ts", enable_cache=True)

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|host_statuses',
                'proxy.config.http.cache.range.write': 1,
                'proxy.config.http.insert_response_via_str': 3,
            })

        self._ts.Disk.remap_config.AddLine(f'map http://backend.example.com/ http://127.0.0.1:{self._server.Variables.http_port}/')

    def run(self):
        # Phase 1: prime the cache with the full object.
        prime = Test.AddTestRun("Prime cache with full object")
        prime.AddVerifierClientProcess("prime-client", self.prime_replay, http_ports=[self._ts.Variables.port])
        prime.Processes.Default.StartBefore(self._server)
        prime.Processes.Default.StartBefore(self._ts)
        prime.StillRunningAfter = self._server
        prime.StillRunningAfter = self._ts

        # Phase 2: mark the origin host DOWN via JSON-RPC.
        mark_down = Test.AddTestRun("Mark host DOWN")
        mark_down.AddJsonRPCClientRequest(
            self._ts, Request.admin_host_set_status(operation='down', host=['127.0.0.1'], reason='manual', time='0'))
        mark_down.StillRunningAfter = self._server
        mark_down.StillRunningAfter = self._ts

        # Phase 3: send an out-of-order multi-range request. The cache cannot
        # satisfy the range, so ATS must fall back to the origin; with the host
        # marked DOWN it should return 502 Bad Gateway. This is a regression
        # test for a stack-overflow crash where this scenario instead drove an
        # unbounded recursion between the cache-fallback and DNS-lookup paths.
        range_run = Test.AddTestRun("Out-of-order Range against DOWN host")
        range_run.AddVerifierClientProcess("range-client", self.range_replay, http_ports=[self._ts.Variables.port])
        range_run.Processes.Default.TimeOut = 10
        range_run.StillRunningAfter = self._server
        range_run.StillRunningAfter = self._ts


HostDownRangeRecursionTest().run()
