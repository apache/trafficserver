'''
Verify that stats_over_http renders Prometheus output from its cache as metrics appear and change.
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
import sys

Test.Summary = 'Render Prometheus stats from the cache as metrics appear and change'
Test.SkipUnless(Condition.PluginExists('stats_over_http.so'))
Test.ContinueOnFail = True


class TestStatsOverHttpPrometheusCache:
    '''Verify the Prometheus output that the plugin renders from its cache as metrics appear and change.'''

    # The path, whether its families have HELP lines, and whether it is the v2 format.
    ENDPOINTS: list[tuple[str, bool, bool]] = [
        ('/metrics/help/v2', True, True),
        ('/metrics/help/v1', True, False),
        ('/metrics/nohelp/v2', False, True),
        ('/metrics/nohelp/v1', False, False),
        ('/_stats/prometheus_v2', False, True),
        ('/_stats/prometheus', False, False),
    ]

    def __init__(self) -> None:
        self._configure_traffic_server()
        self._configure_client()
        self._test_first_metrics()
        self._test_new_and_changed_metrics()
        self._test_concurrent_scrapes()

    def _configure_traffic_server(self) -> None:
        ts = Test.MakeATSProcess('ts')
        self._ts = ts
        Test.PrepareTestPlugin(
            os.path.join(Test.Variables.AtsTestPluginsDir, 'test_metrics.so'), ts,
            '--count=2000 plugin.test_metrics.requests.get=1 plugin.test_metrics.queue=4')
        ts.Disk.plugin_config.AddLine('stats_over_http.so --no-prometheus-help _stats')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'test_metrics|stats_over_http',
            })
        for path, options in [
            ('help/v2', '--format=prometheus_v2'),
            ('help/v1', '--format=prometheus'),
            ('nohelp/v2', '--format=prometheus_v2 @pparam=--no-prometheus-help'),
            ('nohelp/v1', '--format=prometheus @pparam=--no-prometheus-help'),
        ]:
            ts.Disk.remap_config.AddLine(
                f'map http://127.0.0.1:{ts.Variables.port}/metrics/{path} http://127.0.0.1/'
                f' @plugin=stats_over_http.so @pparam={options}')

        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'Assigned plugin.test_metrics.late=7', 'test_metrics.so should create the new gauge.')

    def _configure_client(self) -> None:
        self._check = os.path.join(Test.RunDirectory, 'check_prometheus.py')
        Test.Setup.Copy('check_prometheus.py')
        Test.Setup.Copy('fetch_stats.py')
        Test.Setup.Copy('prometheus_stats_ingester.py')

    def _test_first_metrics(self) -> None:
        for i, (path, help_lines, v2) in enumerate(self.ENDPOINTS):
            if v2:
                expect = ['plugin_test_metrics_requests{method="get"} 1', 'plugin_test_metrics_queue 4']
                families = ['plugin_test_metrics_requests=1']
                if help_lines:
                    expect.append('# HELP plugin_test_metrics_requests plugin.test_metrics.requests.get')
            else:
                expect = ['plugin_test_metrics_requests_get 1', 'plugin_test_metrics_queue 4']
                families = ['plugin_test_metrics_requests_post=0']
            self._scrape('Scrape the first metrics', path, help_lines, expect, families, start=(i == 0))

    def _test_new_and_changed_metrics(self) -> None:
        ts = self._ts
        # A new sample for an existing family, a new family and a changed value.
        assignments = ['plugin.test_metrics.requests.post=2', 'plugin.test_metrics.late=7', 'plugin.test_metrics.requests.get=-5']
        for assignment in assignments:
            tr = Test.AddTestRun(f'Assign {assignment}')
            tr.Processes.Default.Command = f'traffic_ctl plugin msg test_metrics {assignment}'
            tr.Processes.Default.Env = ts.Env
            tr.Processes.Default.ReturnCode = 0
            tr.StillRunningAfter = ts

        for path, help_lines, v2 in self.ENDPOINTS:
            if v2:
                expect = [
                    'plugin_test_metrics_requests{method="get"} -5', 'plugin_test_metrics_requests{method="post"} 2',
                    'plugin_test_metrics_late 7'
                ]
                families = ['plugin_test_metrics_requests=2']
            else:
                expect = [
                    'plugin_test_metrics_requests_get -5', 'plugin_test_metrics_requests_post 2', 'plugin_test_metrics_late 7'
                ]
                families = ['plugin_test_metrics_requests_post=1']
            self._scrape('Scrape the new and changed metrics', path, help_lines, expect, families, wait=True)

    def _test_concurrent_scrapes(self) -> None:
        # Renders on other threads that overlap one that uses the cache render without it.  Each body must still be complete.
        for path, help_lines, v2 in [self.ENDPOINTS[0], self.ENDPOINTS[-1]]:
            if v2:
                expect = ['plugin_test_metrics_requests{method="post"} 2', 'plugin_test_metrics_late 7']
                families = ['plugin_test_metrics_requests=2']
            else:
                expect = ['plugin_test_metrics_requests_post 2', 'plugin_test_metrics_late 7']
                families = ['plugin_test_metrics_requests_post=1']
            self._scrape('Scrape concurrently', path, help_lines, expect, families, concurrency=16)

    def _scrape(
            self,
            description: str,
            path: str,
            help_lines: bool,
            expect: list[str],
            families: list[str],
            wait: bool = False,
            concurrency: int = 1,
            start: bool = False) -> None:
        '''Scrape one endpoint and check its samples and families.'''
        ts = self._ts
        tr = Test.AddTestRun(f'{description}: {path}')
        if start:
            tr.Processes.Default.StartBefore(ts)
        command = f'{sys.executable} {self._check} {ts.Variables.port} {path} --concurrency={concurrency}'
        command += ' --help-lines' if help_lines else ''
        command += ' --wait' if wait else ''
        command += ''.join(f" --expect='{line}'" for line in expect)
        command += ''.join(f' --family={family}' for family in families)
        tr.Processes.Default.Command = command
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('Verified', f'The {path} output should be valid.')
        tr.StillRunningAfter = ts


TestStatsOverHttpPrometheusCache()
