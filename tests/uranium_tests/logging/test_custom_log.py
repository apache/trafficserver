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

from uranium_testkit.scenario import All, Any, Condition, Testers, UraniumTest, When


def test_custom_log(urtest: UraniumTest) -> None:
    '''
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

    urtest.Summary = '''
    Test custom log file format
    '''

    urtest.SkipIf(Condition.CurlUsingUnixDomainSocket())
    # this test depends on Linux specific behavior regarding loopback addresses
    urtest.SkipUnless(Condition.IsPlatform("linux"))

    # Define default ATS
    ts = urtest.MakeATSProcess("ts")

    # setup some config file for this server
    ts.Disk.remap_config.AddLine('map / http://www.linkedin.com/ @action=deny')

    ts.Disk.logging_yaml.AddLines(
        '''
    logging:
      formats:
        - name: custom
          format: "%<hii> %<hiih>"
      logs:
        - filename: test_log_field
          format: custom
    '''.split("\n"))

    # #########################################################################
    # at the end of the different test run a custom log file should exist
    # Because of this we expect the testruns to pass the real test is if the
    # customlog file exists and passes the format check
    urtest.Disk.File(os.path.join(ts.Variables.LOGDIR, 'test_log_field.log'), exists=True, content='gold/custom.gold')

    # first test is a miss for default
    tr = urtest.AddTestRun()
    tr.MakeCurlCommand('"http://127.0.0.1:{0}" --verbose'.format(ts.Variables.port), ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.StartBefore(urtest.Processes.ts)

    tr = urtest.AddTestRun()
    tr.MakeCurlCommand('"http://127.1.1.1:{0}" --verbose'.format(ts.Variables.port), ts=ts)
    tr.Processes.Default.ReturnCode = 0

    tr = urtest.AddTestRun()
    tr.MakeCurlCommand('"http://127.2.2.2:{0}" --verbose'.format(ts.Variables.port), ts=ts)
    tr.Processes.Default.ReturnCode = 0

    tr = urtest.AddTestRun()
    tr.MakeCurlCommand('"http://127.3.3.3:{0}" --verbose'.format(ts.Variables.port), ts=ts)
    tr.Processes.Default.ReturnCode = 0

    tr = urtest.AddTestRun()
    tr.MakeCurlCommand('"http://127.3.0.1:{0}" --verbose'.format(ts.Variables.port), ts=ts)
    tr.Processes.Default.ReturnCode = 0

    tr = urtest.AddTestRun()
    tr.MakeCurlCommand('"http://127.43.2.1:{0}" --verbose'.format(ts.Variables.port), ts=ts)
    tr.Processes.Default.ReturnCode = 0

    tr = urtest.AddTestRun()
    tr.MakeCurlCommand('"http://127.213.213.132:{0}" --verbose'.format(ts.Variables.port), ts=ts)
    tr.Processes.Default.ReturnCode = 0

    tr = urtest.AddTestRun()
    tr.MakeCurlCommand('"http://127.123.32.243:{0}" --verbose'.format(ts.Variables.port), ts=ts)
    tr.Processes.Default.ReturnCode = 0

    # Wait for all expected log lines to be written.
    urtest.AddAwaitFileContainsTestRun(
        'Await custom log lines.',
        os.path.join(ts.Variables.LOGDIR, 'test_log_field.log'),
        r'^127\.',
        8,
    )
    urtest.execute()
