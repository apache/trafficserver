'''
Verify that the stats_over_http remap intercept serves many metrics and releases each request.
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
import re
import sys

Test.Summary = 'Serve many metrics from stats_over_http remap rules and release each request'
Test.SkipUnless(Condition.PluginExists('stats_over_http.so'))
Test.SkipIf(Condition.CurlUsingUnixDomainSocket())
Test.ContinueOnFail = True


class TestStatsOverHttpRemapIntercept:
    '''Verify that remap rules serve many metrics and that each request releases its continuations.'''

    METRIC_COUNT: int = 10000

    def __init__(self) -> None:
        self._configure_traffic_server()
        self._configure_client()
        self._test_formats()
        self._test_request_release()
        self._test_stats_after_aborted_requests()

    def _configure_traffic_server(self) -> None:
        # LeakSanitizer does not run on macOS, so cont_count.py counts live continuations to detect a leaked intercept or
        # transaction hook.
        ts = Test.MakeATSProcess('ts', command='traffic_server --disable_pfreelist')
        self._ts = ts
        Test.PrepareTestPlugin(
            os.path.join(Test.Variables.AtsTestPluginsDir, 'test_metrics.so'), ts, f'--count={self.METRIC_COUNT}')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'stats_over_http',
                'proxy.config.dump_mem_info_frequency': 1,
            })
        # A remap rule matches a path prefix, so the prometheus_v2 rule comes before the prometheus rule.
        for stats_format in ['prometheus_v2', 'prometheus', 'json', 'csv']:
            ts.Disk.remap_config.AddLine(
                f'map http://127.0.0.1:{ts.Variables.port}/_stats/{stats_format} http://127.0.0.1/'
                f' @plugin=stats_over_http.so @pparam=--format={stats_format}')

        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'Intercept finished on TS_EVENT_NET_ACCEPT_FAILED', 'The rejected request should release its intercept.')
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'Intercept finished on .*TS_EVENT_(VCONN_EOS|ERROR)', 'The reset request should release its intercept.')

    def _configure_client(self) -> None:
        self._client = os.path.join(Test.RunDirectory, 'fetch_stats.py')
        Test.Setup.Copy('fetch_stats.py')
        self._cont_count = os.path.join(Test.RunDirectory, 'cont_count.py')
        Test.Setup.Copy('cont_count.py')
        self._cont_baseline = os.path.join(Test.RunDirectory, 'cont_baseline')

    def _test_formats(self) -> None:
        for i, stats_format in enumerate(['json', 'csv', 'prometheus', 'prometheus_v2']):
            self._fetch(f'Fetch {stats_format} stats from a remap rule', stats_format, start=(i == 0))

    def _test_request_release(self) -> None:
        ts = self._ts
        tr = Test.AddTestRun('Count the continuations in use before the GET, HEAD, POST, rejected and reset requests')
        tr.Processes.Default.Command = (
            f'{sys.executable} {self._cont_count} {ts.Disk.traffic_out.Name} {self._cont_baseline} --save')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = ts

        self._fetch('Fetch stats once more', 'prometheus')

        tr = Test.AddTestRun('Send a HEAD request')
        tr.Processes.Default.Command = f'{sys.executable} {self._client} {ts.Variables.port} prometheus --head'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('Response status 200', 'HEAD should succeed.')
        tr.StillRunningAfter = ts

        tr = Test.AddTestRun('Send a POST request')
        tr.Processes.Default.Command = f'{sys.executable} {self._client} {ts.Variables.port} prometheus --post'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('Response status 405', 'POST should get a 405.')
        tr.StillRunningAfter = ts

        tr = Test.AddTestRun('Send a request that Traffic Server rejects after the plugin sets up the intercept')
        tr.Processes.Default.Command = f'{sys.executable} {self._client} {ts.Variables.port} prometheus --reject'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'Response status 406', 'Traffic Server should reject the request.')
        tr.StillRunningAfter = ts

        tr = Test.AddTestRun('Reset the client connection during a response')
        tr.Processes.Default.Command = f'{sys.executable} {self._client} {ts.Variables.port} prometheus --reset'
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = ts

        tr = Test.AddTestRun('Count the continuations in use after the requests')
        tr.Processes.Default.Command = f'{sys.executable} {self._cont_count} {ts.Disk.traffic_out.Name} {self._cont_baseline}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'no more than the baseline', 'The requests should release their continuations.')
        tr.StillRunningAfter = ts

    def _test_stats_after_aborted_requests(self) -> None:
        self._fetch('Fetch stats after the aborted requests', 'json')

    def _fetch(self, description: str, stats_format: str, start: bool = False) -> None:
        '''Fetch the stats in one format and check that its remap rule serves every test metric.'''
        ts = self._ts
        tr = Test.AddTestRun(description)
        if start:
            tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Command = (
            f'{sys.executable} {self._client} {ts.Variables.port} {stats_format} --count={self.METRIC_COUNT}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            f'Verified {self.METRIC_COUNT} test metrics', f'The {stats_format} body should hold every test metric.')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            rf'^X-Stats-Format: {stats_format}$', f'The {stats_format} rule should serve the request.', reflags=re.MULTILINE)
        tr.StillRunningAfter = ts


TestStatsOverHttpRemapIntercept()
