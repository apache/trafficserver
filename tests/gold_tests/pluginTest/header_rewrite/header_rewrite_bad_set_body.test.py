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
Verify header_rewrite rejects invalid set-body configurations during startup.
'''

Test.Summary = '''
header_rewrite must reject ambiguous MIME arguments and unusable local body
files rather than silently serving a different response.
'''

Test.SkipUnless(Condition.PluginExists('header_rewrite.so'))


class TestBadSetBody:
    '''Verify invalid set-body configurations fail to load.'''

    @staticmethod
    def _configure_failure(
        name: str,
        rule_lines: list[str],
        error_marker: str,
        records: dict[str, int] | None = None,
        files: dict[str, str] | None = None,
    ) -> None:
        '''Configure one startup rejection scenario.'''
        ts = Test.MakeATSProcess(name, disable_log_checks=True)
        if records:
            ts.Disk.records_config.update(records)
        if files:
            for filename, content in files.items():
                ts.Disk.MakeConfigFile(filename).WriteOn(content)

        rule_name = f'{name}.conf'
        ts.Disk.MakeConfigFile(rule_name).AddLines(rule_lines)
        ts.Disk.remap_config.AddLine(
            f'map http://{name}.example.com/ http://127.0.0.1/ @plugin=header_rewrite.so @pparam={rule_name}'
        )

        ts.ReturnCode = 33
        ts.Ready = 0
        ts.Disk.diags_log.Content = Testers.IncludesExpression(error_marker, f'{name} must report why the rule was rejected')
        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            'Traffic Server is fully initialized', f'{name} must prevent startup'
        )

        tr = Test.AddTestRun(f'{name} configuration fails startup')
        tr.Processes.Default.Command = 'echo verifying startup rejection'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(ts)

    def __init__(self) -> None:
        '''Configure all rejected set-body scenarios.'''
        hook = 'cond %{REMAP_PSEUDO_HOOK}'

        self._configure_failure(
            'missing-body-file',
            [hook, '  set-body-from-file no-such-body.json application/json'],
            "unable to load body file.*no-such-body.json",
        )
        self._configure_failure(
            'oversized-body-file',
            [hook, '  set-body-from-file too-large-body.json application/json'],
            "exceeds proxy.config.body_factory.response_max_size.*8",
            records={'proxy.config.body_factory.response_max_size': 8},
            files={'too-large-body.json': '{"error":"too large"}'},
        )
        self._configure_failure(
            'ambiguous-body-arguments',
            [hook, '  set-body Sorry, page not found'],
            'accepts at most two arguments',
        )
        self._configure_failure(
            'invalid-body-mime',
            [hook, '  set-body Sorry not-a-mime-type'],
            "Content-Type must be a MIME type containing '/'",
        )


TestBadSetBody()
