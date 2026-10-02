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
'''
Verify header_rewrite rejects invalid cache-key configurations during startup.
'''

Test.Summary = '''
header_rewrite must reject cache-key operators on hooks that run after the
cache lookup, and URL components that an operator can't change.
'''

Test.SkipUnless(Condition.PluginExists('header_rewrite.so'))


class TestBadCacheKey:
    '''Verify invalid cache-key configurations fail to load.'''

    @staticmethod
    def _configure_failure(name: str, rule_lines: list[str], error_marker: str) -> None:
        '''Configure one startup rejection scenario.'''
        ts = Test.MakeATSProcess(name, disable_log_checks=True)

        rule_name = f'{name}.conf'
        ts.Disk.MakeConfigFile(rule_name).AddLines(rule_lines)
        ts.Disk.remap_config.AddLine(
            f'map http://{name}.example.com/ http://127.0.0.1/ '
            f'@plugin=header_rewrite.so @pparam={rule_name}')

        ts.ReturnCode = 33
        ts.Ready = 0
        ts.Disk.diags_log.Content = Testers.IncludesExpression(error_marker, f'{name} must report why the rule was rejected')
        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            'Traffic Server is fully initialized', f'{name} must prevent startup')

        tr = Test.AddTestRun(f'{name} configuration fails startup')
        tr.Processes.Default.Command = 'echo verifying startup rejection'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(ts)

    def __init__(self) -> None:
        '''Configure all rejected cache-key scenarios.'''
        for name, hook, operator, logged in [
            ('set-key-send-response', 'SEND_RESPONSE_HDR_HOOK', 'set-cache-key HOST k.ex', r'set-cache-key\(HOST\)'),
            ('add-key-send-request', 'SEND_REQUEST_HDR_HOOK', 'add-cache-key x', r'add-cache-key\(x\)'),
            ('clear-key-read-response', 'READ_RESPONSE_HDR_HOOK', 'clear-cache-key', r'clear-cache-key\(\)'),
            ('rm-key-send-response', 'SEND_RESPONSE_HDR_HOOK', 'rm-cache-key QUERY', r'rm-cache-key\(QUERY\)'),
            ('sort-key-send-request', 'SEND_REQUEST_HDR_HOOK', 'sort-cache-key QUERY', r'sort-cache-key\(QUERY\)'),
        ]:
            self._configure_failure(
                name,
                [f'cond %{{{hook}}}', f'  {operator}'],
                f"can't use this operator in hook=TS_HTTP_{hook}: +{logged}",
            )

        self._configure_failure(
            'set-key-url',
            ['set-cache-key URL http://k.ex/'],
            'set-cache-key accepts HOST, PORT, PATH, QUERY, or SCHEME, got: URL',
        )
        self._configure_failure(
            'rm-key-host',
            ['rm-cache-key HOST'],
            'rm-cache-key accepts QUERY or PATH, got: HOST',
        )
        self._configure_failure(
            'rm-key-path-names',
            ['rm-cache-key PATH a,b'],
            'rm-cache-key accepts a list of names only for QUERY',
        )
        self._configure_failure(
            'sort-key-path',
            ['sort-cache-key PATH'],
            'sort-cache-key accepts only QUERY, got: PATH',
        )


TestBadCacheKey()
