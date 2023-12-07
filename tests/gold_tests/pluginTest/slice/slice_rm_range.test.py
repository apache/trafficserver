'''
Verify ATS slice plugin config: @pparam=--strip-range-for-head
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
Verify ATS slice plugin config: @pparam=--strip-range-for-head
'''
Test.SkipUnless(
    Condition.PluginExists('slice.so'),
    Condition.PluginExists('cache_range_requests.so'),
)


class SliceStripRangeForHeadRequestTest:
    replay_file = "replay/slice_range.replay.yaml"

    def __init__(self):
        """Initialize the Test processes for the test runs."""
        self._server = Test.MakeVerifierServerProcess("server", SliceStripRangeForHeadRequestTest.replay_file)
        self._configure_trafficserver()

    def _configure_trafficserver(self):
        """Configure Traffic Server."""
        self._ts = Test.MakeATSProcess("ts", enable_cache=False)

        self._ts.Disk.remap_config.AddLines(
            [
                f"map /no/range http://127.0.0.1:{self._server.Variables.http_port} \
                @plugin=slice.so @pparam=--blockbytes-test=10 @pparam=--strip-range-for-head \
                @plugin=cache_range_requests.so",
                f"map /with/range http://127.0.0.1:{self._server.Variables.http_port} \
                @plugin=slice.so @pparam=--blockbytes-test=10 \
                @plugin=cache_range_requests.so",
            ])

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|slice|cache_range_requests',
            })

    def _test_head_request_range_header(self):
        tr = Test.AddTestRun()

        tr.AddVerifierClientProcess("client", SliceStripRangeForHeadRequestTest.replay_file, http_ports=[self._ts.Variables.port])

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

    def run(self):
        self._test_head_request_range_header()


SliceStripRangeForHeadRequestTest().run()
