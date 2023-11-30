'''
Verify ATS slice plugin config accepts PURGE requests
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

Test.Summary = '''
Verify ATS slice plugin config accepts PURGE requests
'''
Test.SkipUnless(
    Condition.PluginExists('slice.so'),
    Condition.PluginExists('cache_range_requests.so'),
)


class SlicePurgeRequestTest:
    replay_file = "replay/slice_purge.replay.yaml"
    replay_ref_file = "replay/slice_purge_ref.replay.yaml"
    replay_no_ref_file = "replay/slice_purge_no_ref.replay.yaml"

    def __init__(self, ref_block, num):
        """Initialize the Test processes for the test runs."""
        self._ref_block = ref_block
        self._num = num
        self._server = Test.MakeVerifierServerProcess(f"server_{self._num}", SlicePurgeRequestTest.replay_file)
        self._configure_trafficserver()

    def _configure_trafficserver(self):
        """Configure Traffic Server."""
        self._ts = Test.MakeATSProcess(f"ts_{self._num}", enable_cache=True)

        if (self._ref_block):
            self._ts.Disk.remap_config.AddLines(
                [
                    f"map /ref/block http://127.0.0.1:{self._server.Variables.http_port} \
                @plugin=slice.so @pparam=--blockbytes-test=10 \
                @plugin=cache_range_requests.so",
                ])
        else:
            self._ts.Disk.remap_config.AddLines(
                [
                    f"map /ref/block http://127.0.0.1:{self._server.Variables.http_port} \
                @plugin=slice.so @pparam=--blockbytes-test=10 @pparam=--ref-relative \
                @plugin=cache_range_requests.so",
                ])

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|slice|cache_range_requests',
            })

    def slice_purge(self):
        tr = Test.AddTestRun()

        tr.AddVerifierClientProcess(f"client_{self._num}", SlicePurgeRequestTest.replay_file, http_ports=[self._ts.Variables.port])

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

    def slice_purge_ref(self):
        tr = Test.AddTestRun()

        tr.AddVerifierClientProcess("client_ref", SlicePurgeRequestTest.replay_ref_file, http_ports=[self._ts.Variables.port])

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

    def slice_purge_no_ref(self):
        tr = Test.AddTestRun()

        tr.AddVerifierClientProcess("client_no_ref", SlicePurgeRequestTest.replay_no_ref_file, http_ports=[self._ts.Variables.port])

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)


SlicePurgeRequestTest(True, 0).slice_purge()
SlicePurgeRequestTest(True, 1).slice_purge_ref()
SlicePurgeRequestTest(False, 2).slice_purge()
SlicePurgeRequestTest(False, 3).slice_purge_no_ref()
