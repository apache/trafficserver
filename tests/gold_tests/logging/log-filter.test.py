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

Test.Summary = '''
Test log filter.
'''

ts = Test.MakeATSProcess("ts", enable_cache=False)
replay_file = "log-filter.replays.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)

ts.Disk.records_config.update({
    'proxy.config.net.connections_throttle': 100,
})
# setup some config file for this server
ts.Disk.remap_config.AddLine(
    'map / http://localhost:{}/'.format(server.Variables.http_port)
)

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  filters:
    - name: queryparamescaper_cquuc
      action: WIPE_FIELD_VALUE
      condition: cquuc CASE_INSENSITIVE_CONTAIN password,secret,access_token,session_redirect,cardNumber,code,query,search-query,prefix,keywords,email,handle
  formats:
    - name: custom
      format: '%<cquuc>'
  logs:
    - filename: filter-test
      format: custom
      filters:
      - queryparamescaper_cquuc
'''.split("\n")
)

# #########################################################################
# at the end of the different test run a custom log file should exist
# Because of this we expect the testruns to pass the real test is if the
# customlog file exists and passes the format check
Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'filter-test.log'),
               exists=True, content='gold/filter-test.gold')

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client-1", replay_file, http_ports=[ts.Variables.port])

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
    os.path.join(ts.Variables.LOGDIR, 'filter-test.log')
)
test_run.Processes.Default.ReturnCode = 0
