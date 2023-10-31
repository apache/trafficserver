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

Test.Summary = 'Test TSContScheduleEveryOnThread API'
Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess('ts')

Test.testName = 'Test TSContScheduleEveryOnThread API'

ts.Disk.records_config.update(
    {
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.autoconfig.scale': 1.5,
        'proxy.config.exec_thread.limit': 32,
        'proxy.config.accept_threads': 1,
        'proxy.config.task_threads': 2,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'TSContSchedule_test'
    })

# Load plugin
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'cont_schedule.so'), ts, 'every_thread')

# www.example.com Host
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'printf "Test TSContScheduleEveryOnThread API"'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)

tr = Test.AddTestRun("Wait traffic.out to be written")
timeout = 30
watcher = tr.Processes.Process("watcher")
watcher.Command = f"sleep {timeout}"
watcher.Ready = When.FileExists(ts.Disk.traffic_out.AbsPath)
watcher.TimeOut = timeout
tr.TimeOut = timeout
tr.DelayStart = 3
tr.Processes.Default.StartBefore(watcher)
tr.Processes.Default.Command = 'printf "await traffic.out"'
tr.Processes.Default.ReturnCode = 0
ts.Disk.traffic_out.Content = "gold/schedule_every_on_thread.gold"
ts.Disk.traffic_out.Content += Testers.IncludesExpression('pass', 'must contain "pass"')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression('fail', 'should not contain "fail"')
