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


def test_schedule_every_on_entire_pool(urtest: UraniumTest) -> None:
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
    import sys

    urtest.Summary = 'Test TSContScheduleEveryOnEntirePool API'
    urtest.ContinueOnFail = True

    # Define default ATS
    ts = urtest.MakeATSProcess('ts')

    urtest.testName = 'Test TSContScheduleEveryOnEntirePool API'

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

    ts.Setup.Copy('entire_pool.py')

    # Load plugin
    urtest.PrepareTestPlugin(os.path.join(urtest.Variables.AtsTestPluginsDir, 'cont_schedule.so'), ts, 'every_entire')

    # www.example.com Host
    tr = urtest.AddTestRun()
    tr.Processes.Default.Command = 'printf "Test TSContScheduleEveryOnEntirePool API"'
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.StartBefore(ts)

    tr = urtest.AddTestRun("Wait traffic.out to be written")
    timeout = 30
    watcher = tr.Processes.Process("watcher")
    watcher.Command = f"sleep {timeout}"
    watcher.Ready = When.FileExists(ts.Disk.traffic_out.AbsPath)
    watcher.TimeOut = timeout
    tr.TimeOut = timeout
    tr.DelayStart = 3
    tr.Processes.Default.StartBefore(watcher)
    tr.Processes.Default.Command = f'{sys.executable} entire_pool.py {ts.Disk.traffic_out.AbsPath} ET_NET 32 2'
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.All = "gold/schedule_every_on_entire_pool.gold"
    tr.Processes.Default.Streams.All += Testers.ExcludesExpression('fail', 'should not contain "fail"')
    urtest.execute()
