'''
Verify that invalid jax_fingerprint configurations are rejected.
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

Test.Summary = __doc__
Test.SkipUnless(Condition.PluginExists('jax_fingerprint.so'))


class InvalidConfigTest:
    '''Verify that an invalid configuration is rejected.'''

    _ts_counter: int = 0

    def __init__(self, name: str, config: str, expected_error: str) -> None:
        '''Configure Traffic Server with an invalid jax_fingerprint configuration.

        :param name: Descriptive name for this test run.
        :param config: The YAML configuration content.
        :param expected_error: A regular expression matching the error logged for the configuration.
        '''
        self._ts = Test.MakeATSProcess(f'ts_invalid{InvalidConfigTest._ts_counter}', enable_cache=False)
        InvalidConfigTest._ts_counter += 1

        config_name = 'jax_fingerprint.yaml'
        config_path = os.path.join(self._ts.Variables.CONFIGDIR, config_name)
        self._ts.Disk.File(config_path, id='jax_config', typename='ats:config')
        self._ts.Disk.jax_config.AddLines(config.strip().split('\n'))
        self._ts.Disk.plugin_config.AddLine(f'jax_fingerprint.so {config_name}')

        # Replace the default "no errors" check since this test expects configuration errors.
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(expected_error, 'Verify the configuration error is reported.')
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Failed to load configuration from .*jax_fingerprint\.yaml', 'Verify the configuration is rejected.')

        tr = Test.AddTestRun(name)
        tr.Processes.Default.Command = 'echo "Traffic Server started"'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts
        Test.AddAwaitFileContainsTestRun(
            f'Await the configuration error for: {name}', self._ts.Disk.diags_log.AbsPath, 'Failed to load configuration')


InvalidConfigTest(
    'Reject unknown configuration keys', '''
jax_fingerprint:
  fingerprints:
    - method: JA4H
      standalone: true
      via-header: x-jax-via
''', r"Unknown key 'via-header' in fingerprint entry")

InvalidConfigTest(
    'Reject duplicate fingerprint settings', '''
jax_fingerprint:
  fingerprints:
    - method: JA4H
      standalone: false
      standalone: true
''', r"Duplicate key 'standalone' in fingerprint entry")

InvalidConfigTest(
    'Reject duplicate fingerprints lists', '''
jax_fingerprint:
  fingerprints:
    - method: JA4H
      standalone: true
  fingerprints:
    - method: JA3
''', r"Duplicate key 'fingerprints' in jax_fingerprint")

InvalidConfigTest(
    'Reject empty server names', '''
jax_fingerprint:
  fingerprints:
    - method: JA4
      servernames:
        - ""
''', r"Each 'servernames' entry must be a non-empty server name")

InvalidConfigTest(
    'Reject servernames for request-based methods', '''
jax_fingerprint:
  fingerprints:
    - method: JA4H
      standalone: true
      servernames:
        - abc.example
''', r"'servernames' is not supported for JA4H")

InvalidConfigTest(
    'Reject a method listed twice for one registry', '''
jax_fingerprint:
  fingerprints:
    - method: JA4
      standalone: true
      header: x-ja4-a
    - method: JA4
      standalone: true
      header: x-ja4-b
      export: jax_fingerprint
''', r"method JA4 is listed more than once for registry 'jax_fingerprint'")

InvalidConfigTest(
    'Reject a log_field listed twice', '''
jax_fingerprint:
  fingerprints:
    - method: JA3
      log_field: jaxfp
    - method: JA4
      log_field: jaxfp
''', r"log_field 'jaxfp' is listed more than once")
