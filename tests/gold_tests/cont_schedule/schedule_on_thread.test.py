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

Test.Summary = 'Test TSContScheduleOnThread API'
Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess('ts')

Test.testName = 'Test TSContScheduleOnThread API'

ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig.enabled': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 32,
    'proxy.config.accept_threads': 1,
    'proxy.config.task_threads': 2,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'TSContSchedule_test'
})

# Load plugin
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'cont_schedule.so'), ts, 'thread')

# www.example.com Host
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'printf "Test TSContScheduleOnThread API"'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
ts.Disk.traffic_out.Content = "gold/schedule_on_thread.gold"
ts.Disk.traffic_out.Content += Testers.ExcludesExpression('fail', 'should not contain "fail"')
