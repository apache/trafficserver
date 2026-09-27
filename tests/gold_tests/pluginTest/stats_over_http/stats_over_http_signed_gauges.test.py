'''
Verify that stats_over_http prints negative gauges as signed values and applies --wrap-counters only to counters.
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
from typing import Any

Test.Summary = 'stats_over_http prints negative gauges as signed values and wraps only counters'
Test.SkipUnless(Condition.PluginExists('stats_over_http.so'))
Test.ContinueOnFail = True


class TestStatsOverHttpSignedGauges:
    '''Verify that negative gauges print as signed values and that --wrap-counters wraps only counters.'''

    TEST_METRICS: str = 'plugin.test_metrics.negative=-42 --counter=plugin.test_metrics.large_counter=9223372036854775850'

    def __init__(self) -> None:
        self._configure_traffic_server()
        self._configure_client()
        self._test_signed_gauges()
        self._test_wrap_counters()

    def _configure_traffic_server(self) -> None:
        self._ts = self._make_traffic_server('ts', '_stats')
        self._ts_wrap = self._make_traffic_server('ts_wrap', '--wrap-counters _stats')

    def _make_traffic_server(self, name: str, options: str) -> Any:
        ts = Test.MakeATSProcess(name)
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'test_metrics.so'), ts, self.TEST_METRICS)
        ts.Disk.plugin_config.AddLine(f'stats_over_http.so {options}')
        return ts

    def _configure_client(self) -> None:
        self._client = os.path.join(Test.RunDirectory, 'fetch_stats.py')
        Test.Setup.Copy('fetch_stats.py')

    def _test_signed_gauges(self) -> None:
        self._verify(
            'Print a negative gauge as a signed value and a counter above INT64_MAX unchanged', self._ts, {
                'json':
                    [['"plugin.test_metrics.negative": "-42",'], ['"plugin.test_metrics.large_counter": "9223372036854775850",']],
                'csv': [['plugin.test_metrics.negative,-42'], ['plugin.test_metrics.large_counter,9223372036854775850']],
                'prometheus': self._prometheus('9223372036854775850'),
                'prometheus_v2': self._prometheus('9223372036854775850'),
            })

    def _test_wrap_counters(self) -> None:
        self._verify(
            'With --wrap-counters, wrap a counter above INT64_MAX and still print a negative gauge as a signed value',
            self._ts_wrap, {
                'json': [['"plugin.test_metrics.negative": "-42",'], ['"plugin.test_metrics.large_counter": "43",']],
                'csv': [['plugin.test_metrics.negative,-42'], ['plugin.test_metrics.large_counter,43']],
                'prometheus': self._prometheus('43'),
                'prometheus_v2': self._prometheus('43'),
            })

    @staticmethod
    def _prometheus(counter: str) -> list[list[str]]:
        '''The TYPE line and the sample of each test metric, which both Prometheus formats write one after the other.'''
        return [
            ['# TYPE plugin_test_metrics_negative gauge', 'plugin_test_metrics_negative -42'],
            ['# TYPE plugin_test_metrics_large_counter counter', f'plugin_test_metrics_large_counter {counter}'],
        ]

    @staticmethod
    def _quote(line: str) -> str:
        '''Quote a command argument.  AuTest splits an argument that is not in quotes at each comma.'''
        return "'" + line.replace("'", "'\"'\"'") + "'"

    def _verify(self, description: str, ts: Any, expected: dict[str, list[list[str]]]) -> None:
        '''Check that the stats in each format have each group of lines, one line after the other.

        The response of the global plugin is chunked, so fetch_stats.py checks the decoded body.  A chunk boundary can split
        a line of the raw body.
        '''
        for i, (stats_format, groups) in enumerate(expected.items()):
            tr = Test.AddTestRun(f'{description}: {stats_format}')
            if i == 0:
                tr.Processes.Default.StartBefore(ts)
            options = ''.join(' --expect ' + ' '.join(self._quote(line) for line in group) for group in groups)
            tr.Processes.Default.Command = f'{sys.executable} {self._client} {ts.Variables.port} {stats_format}{options}'
            tr.Processes.Default.ReturnCode = 0
            for group in groups:
                tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                    re.escape(f'Found the lines {group}'), f'The decoded {stats_format} body should have {group}')
            tr.StillRunningAfter = ts


TestStatsOverHttpSignedGauges()
