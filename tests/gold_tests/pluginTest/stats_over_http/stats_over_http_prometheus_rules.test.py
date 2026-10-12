'''
Verify the Prometheus rules of a stats_over_http remap rule, and their reload after the rules file changes.
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
from typing import Any

Test.Summary = 'Serve Prometheus stats named by a rules file, and load the file again after a change and a reload'
Test.SkipUnless(Condition.PluginExists('stats_over_http.so'))
Test.ContinueOnFail = True

Test.ATSReplayTest(replay_file='replay/prometheus_rules.replay.yaml')
Test.ATSReplayTest(replay_file='replay/prometheus_rules_error.replay.yaml')


class TestStatsOverHttpPrometheusRules:
    '''Verify the rules files of remap rules and their reloads, with remap.config and with remap.yaml.'''

    TEST_METRICS: str = 'plugin.test_metrics.requests.get=7 plugin.test_metrics.requests.post=9'
    LABELED: list[str] = ['plugin_test_metrics_requests{method="get"} 7', 'plugin_test_metrics_requests{method="post"} 9']

    def __init__(self) -> None:
        self._configure_traffic_server()
        self._configure_yaml_traffic_server()
        self._configure_client()
        self._test_rules()
        self._test_reloads()
        self._test_failed_reload()
        self._test_yaml_reload()

    def _configure_traffic_server(self) -> None:
        ts = Test.MakeATSProcess('ts_reload')
        self._ts = ts
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'test_metrics.so'), ts, self.TEST_METRICS)
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'stats_over_http',
        })
        self._rules_file = os.path.join(ts.Variables.CONFIGDIR, 'rules.yaml')
        ts.Setup.CopyAs('rules/labels.yaml', ts.Variables.CONFIGDIR, 'rules.yaml')
        ts.Setup.CopyAs('rules/labels.yaml', ts.Variables.CONFIGDIR, 'labels.yaml')
        ts.Setup.CopyAs('rules/settings.yaml', ts.Variables.CONFIGDIR, 'settings.yaml')
        ts.Disk.remap_config.AddLines(
            [
                f'map http://127.0.0.1:{ts.Variables.port}/metrics http://127.0.0.1/'
                ' @plugin=stats_over_http.so @pparam=--config=rules.yaml @pparam=--on-config-error=503',
                f'map http://127.0.0.1:{ts.Variables.port}/labels http://127.0.0.1/'
                ' @plugin=stats_over_http.so @pparam=--config=labels.yaml',
                f'map http://127.0.0.1:{ts.Variables.port}/settings http://127.0.0.1/'
                ' @plugin=stats_over_http.so @pparam=--config=settings.yaml',
            ])

        ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r'rules\.yaml: line 20: prometheus\.rules\.match: .* has no capture group',
            'The plugin should log the error in the rules file.')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Writing plugin\.test_metrics\.requests\.post with the labels \(method\) of plugin_test_metrics_requests instead'
            r' of its own labels \(verb\)', 'The plugin should log the relabeled sample.')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'labels\.yaml: line 20: prometheus\.rules\.match: .* has no capture group',
            'The failed reload should log the error in the rules file.')
        ts.Disk.diags_log.Content += Testers.ExcludesExpression('FATAL:', 'diags.log should not contain fatal errors')
        ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            'Unrecognized configuration value', 'diags.log should not contain a warning about an unrecognized configuration')

    def _configure_yaml_traffic_server(self) -> None:
        # The same reload when Traffic Server loads remap.yaml instead of remap.config.
        ts_yaml = Test.MakeATSProcess('ts_reload_yaml')
        self._ts_yaml = ts_yaml
        Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'test_metrics.so'), ts_yaml, self.TEST_METRICS)
        self._yaml_rules_file = os.path.join(ts_yaml.Variables.CONFIGDIR, 'rules.yaml')
        ts_yaml.Setup.CopyAs('rules/labels.yaml', ts_yaml.Variables.CONFIGDIR, 'rules.yaml')
        ts_yaml.Disk.remap_yaml.AddLines(
            f'''
remap:
  - type: map
    from:
      url: http://127.0.0.1:{ts_yaml.Variables.port}/metrics
    to:
      url: http://127.0.0.1/
    plugins:
      - name: stats_over_http.so
        params:
          - --config=rules.yaml
          - --on-config-error=503
'''.split('\n'))

        ts_yaml.Disk.diags_log.Content += Testers.ContainsExpression(
            r'remap\.yaml finished loading', 'Traffic Server should load remap.yaml.')

    def _configure_client(self) -> None:
        self._check = os.path.join(Test.RunDirectory, 'check_prometheus.py')
        Test.Setup.Copy('check_prometheus.py')
        Test.Setup.Copy('fetch_stats.py')
        Test.Setup.Copy('prometheus_stats_ingester.py')
        Test.Setup.Copy('rules')

    def _test_rules(self) -> None:
        self._scrape('The rules label a family', self.LABELED, start=True)
        self._scrape(
            'The file sets the format and max_age_ms 0, so each request gets a new render', [],
            path='/settings',
            options='--fresh --help-lines')

    def _test_reloads(self) -> None:
        ts = self._ts
        self._install('labels_verb.yaml')
        self._scrape('The rules stay until a reload', ['plugin_test_metrics_requests{method="get"} 7'])
        Test.AddConfigReload(ts, expect_tasks=['remap.config'], description='Reload the changed rules file')
        self._scrape('A reload loads the changed rules', ['plugin_test_metrics_requests{verb="get"} 7'], wait=True)
        self._install('no_capture_group.yaml')
        Test.AddConfigReload(
            ts, expect_tasks=['remap.config'], description='Reload a broken rules file of a rule with --on-config-error=503')
        self._scrape('After a reload with a broken rules file, the rule answers with a 503', [], status=503, wait=True)
        self._install('labels.yaml')
        Test.AddConfigReload(ts, expect_tasks=['remap.config'], description='Reload the fixed rules file')
        self._scrape('A reload after a fix loads the rules again', ['plugin_test_metrics_requests{method="get"} 7'], wait=True)

    def _test_failed_reload(self) -> None:
        ts = self._ts
        self._install('no_capture_group.yaml', target=os.path.join(ts.Variables.CONFIGDIR, 'labels.yaml'))
        Test.AddConfigReload(
            ts,
            expect='fail',
            expect_tasks=['remap.config'],
            description='Without --on-config-error, a broken rules file fails the reload')
        self._scrape('After the failed reload, the old rules stay', self.LABELED, path='/labels')

    def _test_yaml_reload(self) -> None:
        ts_yaml = self._ts_yaml
        self._scrape('With remap.yaml, the rules label a family', self.LABELED, start=True, process=ts_yaml)
        self._install('labels_verb.yaml', process=ts_yaml, target=self._yaml_rules_file)
        Test.AddConfigReload(ts_yaml, description='Reload the changed rules file under remap.yaml')
        self._scrape(
            'With remap.yaml, a reload loads the changed rules', ['plugin_test_metrics_requests{verb="get"} 7'],
            wait=True,
            process=ts_yaml)

    def _scrape(
            self,
            description: str,
            expect: list[str],
            status: int = 200,
            wait: bool = False,
            start: bool = False,
            path: str = '/metrics',
            options: str = '',
            process: Any = None) -> None:
        '''Scrape a path of a Traffic Server process, ts_reload unless another is given, and check the response.'''
        if process is None:
            process = self._ts
        tr = Test.AddTestRun(description)
        if start:
            tr.Processes.Default.StartBefore(process)
        command = f'{sys.executable} {self._check} {process.Variables.port} {path} --status={status} {options}'
        command += ' --wait' if wait else ''
        command += ''.join(f" --expect='{line}'" for line in expect)
        tr.Processes.Default.Command = command
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = process

    def _install(self, rules: str, process: Any = None, target: str | None = None) -> None:
        '''Copy a file of the rules directory over a rules file, by default the rules.yaml of ts_reload.'''
        if process is None:
            process = self._ts
        if target is None:
            target = self._rules_file
        tr = Test.AddTestRun(f'Replace the rules file with {rules}')
        tr.Processes.Default.Command = f'cp {os.path.join(Test.RunDirectory, "rules", rules)} {target}'
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = process


TestStatsOverHttpPrometheusRules()
