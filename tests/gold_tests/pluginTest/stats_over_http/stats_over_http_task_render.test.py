'''
Verify that stats_over_http renders on a task thread while requests wait, including aborts, timeouts and reloads.
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
import shutil
import sys
from typing import Any

Test.Summary = 'Render stats on a task thread while requests wait for the render'
Test.SkipUnless(Condition.PluginExists('stats_over_http.so'))
Test.SkipIf(Condition.CurlUsingUnixDomainSocket())
Test.ContinueOnFail = True


class TestStatsOverHttpTaskRender:
    '''Verify that the plugin renders stats on a task thread while requests wait for the render.'''

    METRIC_COUNT: int = 1000
    REUSE_MAX_AGE_MS: int = 3000
    REMAP_WAIT_TIMEOUT_MS: int = 1000
    GLOBAL_WAIT_TIMEOUT_MS: int = 2000

    def __init__(self) -> None:
        self._configure_traffic_server()
        self._configure_global_traffic_server()
        self._configure_client()
        self._test_continuations_before()
        self._test_scenarios()
        self._test_global_wait_timeouts()
        self._test_continuations_after()
        self._test_waiter_timeouts()

    def _configure_traffic_server(self) -> None:
        # LeakSanitizer does not run on macOS, so cont_count.py counts live continuations to detect a leaked render, watchdog,
        # hook or intercept.
        ts = Test.MakeATSProcess('ts', command='traffic_server --disable_pfreelist')
        self._ts = ts
        Test.PrepareTestPlugin(
            os.path.join(Test.Variables.AtsTestPluginsDir, 'test_metrics.so'), ts, f'--count={self.METRIC_COUNT}')
        ts.Disk.plugin_config.AddLine('stats_over_http.so _stats')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'stats_over_http|test_metrics',
                'proxy.config.dump_mem_info_frequency': 1,
                # A stalled render holds one task thread.  More threads make it likely that a remap reload runs on another.
                'proxy.config.task_threads': 4,
            })
        rules = {
            'concurrent': '@pparam=--format=prometheus',
            'reuse': f'@pparam=--format=json @pparam=--max-age-ms={self.REUSE_MAX_AGE_MS}',
            'close': '@pparam=--format=json',
            'timeout': f'@pparam=--format=prometheus @pparam=--wait-timeout-ms={self.REMAP_WAIT_TIMEOUT_MS}',
            'loop': '@pparam=--format=prometheus @pparam=--max-age-ms=0',
        }
        for name, options in rules.items():
            ts.Disk.remap_config.AddLine(
                f'map http://127.0.0.1:{ts.Variables.port}/metrics/{name} http://127.0.0.1/ @plugin=stats_over_http.so {options}')
        # The reset scenario expects the intercept to finish before the render, which needs allow_half_open=0.
        ts.Disk.remap_config.AddLine(
            f'map http://127.0.0.1:{ts.Variables.port}/metrics/reset http://127.0.0.1/'
            ' @plugin=conf_remap.so @pparam=proxy.config.http.allow_half_open=0 @plugin=stats_over_http.so @pparam=--format=json')
        self._expect_task_thread_renders(ts)

    def _configure_global_traffic_server(self) -> None:
        # A process loads the global plugin once, so the global plugin with a short wait timeout needs a process of its own.
        # With one task thread, a held task can delay a render.
        ts_global = Test.MakeATSProcess('ts_global')
        self._ts_global = ts_global
        Test.PrepareTestPlugin(
            os.path.join(Test.Variables.AtsTestPluginsDir, 'test_metrics.so'), ts_global, f'--count={self.METRIC_COUNT}')
        ts_global.Disk.plugin_config.AddLine(
            f'stats_over_http.so --max-age-ms=0 --wait-timeout-ms={self.GLOBAL_WAIT_TIMEOUT_MS} _stats')
        ts_global.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'stats_over_http|test_metrics',
                'proxy.config.task_threads': 1,
            })
        self._expect_task_thread_renders(ts_global)

    def _expect_task_thread_renders(self, process: Any) -> None:
        '''Check that the process renders the stats only on a task thread.'''
        process.Disk.traffic_out.Content += Testers.ContainsExpression(
            r'\[ET_TASK \d+\] .*Rendered stats instance', 'The plugin should render the stats on a task thread.')
        process.Disk.traffic_out.Content += Testers.ExcludesExpression(
            r'^\[[^\]]*\] (?!\[ET_TASK \d+\] ).*Rendered stats instance',
            'The plugin should render the stats only on a task thread.')

    def _configure_client(self) -> None:
        self._driver = os.path.join(Test.RunDirectory, 'task_render.py')
        Test.Setup.Copy('task_render.py')
        self._client = os.path.join(Test.RunDirectory, 'fetch_stats.py')
        Test.Setup.Copy('fetch_stats.py')
        self._cont_count = os.path.join(Test.RunDirectory, 'cont_count.py')
        Test.Setup.Copy('cont_count.py')
        self._cont_baseline = os.path.join(Test.RunDirectory, 'cont_baseline')

    def _test_continuations_before(self) -> None:
        ts = self._ts
        tr = Test.AddTestRun('Fetch the stats once before counting continuations')
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Command = f'{sys.executable} {self._client} {ts.Variables.port} json --count={self.METRIC_COUNT}'
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = ts

        tr = Test.AddTestRun('Count the continuations in use before the scenarios')
        tr.Processes.Default.Command = (
            f'{sys.executable} {self._cont_count} {ts.Disk.traffic_out.Name} {self._cont_baseline} --save')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = ts

    def _test_scenarios(self) -> None:
        self._run('Answer concurrent requests from one render', 'concurrent', '/metrics/concurrent')
        self._run(
            'Answer requests from one render until it is older than --max-age-ms', 'reuse', '/metrics/reuse',
            f'--max-age-ms={self.REUSE_MAX_AGE_MS}')
        self._run('Reset a connection while its request waits for a render', 'reset', '/metrics/reset')
        self._run('Close a connection while its request waits for a render', 'close', '/metrics/close')
        # task_render.py decodes br with the brotli command.
        brotli = '--brotli' if Test.Variables.get('TS_HAS_BROTLI') and shutil.which('brotli') else ''
        self._run('Answer the global plugin in many formats and encodings', 'global', extra=brotli)
        self._run(
            'Time out a request, then reload remap.config while the render runs', 'timeout-reload', '/metrics/timeout',
            f'--wait-timeout-ms={self.REMAP_WAIT_TIMEOUT_MS}')
        self._run('Reload remap.config while requests render', 'reload-loop', '/metrics/loop')

    def _test_global_wait_timeouts(self) -> None:
        ts_global = self._ts_global
        tr = self._run(
            'Time out a request to the global plugin',
            'global-timeout',
            extra=f'--wait-timeout-ms={self.GLOBAL_WAIT_TIMEOUT_MS}',
            process=ts_global)
        tr.Processes.Default.StartBefore(ts_global)
        self._run(
            'Give a request that waits for a second render its own wait timeout',
            'global-handover',
            extra=f'--wait-timeout-ms={self.GLOBAL_WAIT_TIMEOUT_MS}',
            process=ts_global)
        self._run(
            'Answer a request that arrives during a render from the next render',
            'global-fresh',
            extra=f'--wait-timeout-ms={self.GLOBAL_WAIT_TIMEOUT_MS}',
            process=ts_global)

    def _test_continuations_after(self) -> None:
        ts = self._ts
        tr = Test.AddTestRun('Count the continuations in use after the scenarios')
        tr.Processes.Default.Command = f'{sys.executable} {self._cont_count} {ts.Disk.traffic_out.Name} {self._cont_baseline}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'no more than the baseline', 'The requests, renders and watchdogs should release their continuations.')
        tr.StillRunningAfter = ts

    def _test_waiter_timeouts(self) -> None:
        for process in (self._ts, self._ts_global):
            tr = Test.AddTestRun(f'Count the requests of {process.Name} that timed out waiting for a render')
            tr.Processes.Default.Command = 'traffic_ctl metric get plugin.stats_over_http.waiter_timeouts'
            tr.Processes.Default.Env = process.Env
            tr.Processes.Default.ReturnCode = 0
            tr.Processes.Default.Streams.All = Testers.ContainsExpression(
                r'^plugin\.stats_over_http\.waiter_timeouts\s+[1-9][0-9]*$', 'The timed out requests should be counted.')
            tr.StillRunningAfter = process

    def _run(self, description: str, scenario: str, path: str = '/', extra: str = '', process: Any = None) -> Any:
        '''Run one task_render.py scenario against a Traffic Server process, ts unless another is given.'''
        if process is None:
            process = self._ts
        tr = Test.AddTestRun(description)
        tr.Processes.Default.Command = (
            f'{sys.executable} {self._driver} {process.Variables.port} {process.Disk.traffic_out.AbsPath} {scenario} --path={path} '
            f'--count={self.METRIC_COUNT} --remap-config={process.Disk.remap_config.AbsPath} {extra}')
        tr.Processes.Default.Env = process.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            f'{scenario}: passed', f'The {scenario} scenario should pass.')
        tr.StillRunningAfter = process
        return tr


TestStatsOverHttpTaskRender()
