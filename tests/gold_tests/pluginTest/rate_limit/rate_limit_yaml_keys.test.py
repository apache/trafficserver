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

import copy
import yaml

Test.Summary = 'rate_limit rejects unknown YAML keys at every configuration level.'
Test.SkipUnless(Condition.PluginExists('rate_limit.so'))


class TestYamlKeys:
    """Exercise configuration loading without sending traffic."""

    def __init__(self) -> None:
        config = {
            'lists': [{
                'name': 'local',
                'cidr': ['127.0.0.1/32']
            }],
            'ip-rep':
                [
                    {
                        'name': 'reputation',
                        'buckets': 2,
                        'size': 4,
                        'percentage': 90,
                        'max_age': 300,
                        'perma-block': {
                            'limit': 100,
                            'threshold': 1,
                            'max_age': 1800
                        },
                    }
                ],
            'selector':
                [
                    {
                        'sni': 'test.example.com',
                        'aliases': ['alias.example.com'],
                        'limit': 10,
                        'rate': 0,
                        'queue': {
                            'size': 5,
                            'max_age': 30
                        },
                        'metrics': {
                            'prefix': 'plugin.rate_limit',
                            'tag': 'valid'
                        },
                        'ip-rep': 'reputation',
                        'exclude': 'local',
                    }
                ],
        }
        self._configure('valid', config)
        for name, path, key, context in [
            ('root', (), 'selecter', 'configuration'),
            ('list', ('lists', 0), 'cidrs', 'lists'),
            ('selector', ('selector', 0), 'limti', 'selector'),
            ('queue', ('selector', 0, 'queue'), 'max-age', 'queue'),
            ('metrics', ('selector', 0, 'metrics'), 'prefxi', 'metrics'),
            ('iprep', ('ip-rep', 0), 'max-age', 'ip-rep'),
            ('perma', ('ip-rep', 0, 'perma-block'), 'max-age', 'perma-block'),
        ]:
            invalid = copy.deepcopy(config)
            node = invalid
            for part in path:
                node = node[part]
            node[key] = 1
            self._configure(name, invalid, f"Unknown key '{key}' in {context} node at line [0-9]+")
        for name, config, error in [
            ('misspelled-sni', {'selector': [{'sin': 'test'}]}, "Unknown key 'sin' in selector node at line [0-9]+"),
            ('no-sni', {'selector': [{'limit': 10}]}, 'selector node is not a map or without a name'),
            ('bad-queue', {'selector': [{'sni': 'test', 'queue': []}]}, 'The queue node must be a map'),
            ('bad-metrics', {'selector': [{'sni': 'test', 'metrics': []}]}, 'The metrics node must be a map'),
            ('bad-selector', {'selector': {'sni': 'test'}}, 'The selector node must be a sequence'),
            ('non-scalar-key', {'selector': [{'sni': 'test', 'queue': {('bad', 'key'): 1}}]},
             'The queue node has a non-scalar key at line [0-9]+'),
            ('bad-value', {'selector': [{'sni': 'test', 'limit': 'abc'}]}, 'Invalid value in configuration file'),
            ('empty-config', None, 'The configuration file is empty'),
        ]:
            self._configure(name, config, error)

    @staticmethod
    def _configure(name: str, config: dict | None, error: str | None = None) -> None:
        ts = Test.MakeATSProcess(name, disable_log_checks=error is not None)
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'rate_limit',
        })
        # A None config stands for a file that declares no rules at all.
        lines = yaml.safe_dump(config).splitlines() if config is not None else ['# no rate limiting rules']
        ts.Disk.File(f'{ts.Variables.CONFIGDIR}/rate_limit.yaml', typename='ats:config').AddLines(lines)
        ts.Disk.plugin_config.AddLine(f'rate_limit.so {ts.Variables.CONFIGDIR}/rate_limit.yaml')
        tr = Test.AddTestRun(f'{name}: rate_limit YAML configuration')
        tr.Processes.Default.Command = 'echo configuration checked'
        tr.Processes.Default.ReturnCode = 0
        if error:
            ts.ReturnCode = 70  # EX_SOFTWARE from TSFatal.
            ts.Ready = 0
            ts.Disk.diags_log.Content = Testers.ContainsExpression(error, 'Report the invalid configuration')
            ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                'Traffic Server is fully initialized', 'Invalid configuration prevents startup')
            watcher = Test.Processes.Process(f'{name}-watcher')
            watcher.Command = 'sleep 10'
            watcher.Ready = When.FileContains(ts.Disk.diags_log.Name, 'Failed to parse YAML file')
            watcher.StartBefore(ts)
            tr.TimeOut = 5
            tr.Processes.Default.StartBefore(watcher)
        else:
            ts.Disk.traffic_out.Content += Testers.ContainsExpression('Successfully loaded YAML file', 'Accept all supported keys')
            tr.Processes.Default.StartBefore(ts)


TestYamlKeys()
