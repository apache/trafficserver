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

Test.Summary = 'Test TSEmergency API'
Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess('ts')

Test.testName = 'Emergency Shutdown Test'

ts.Disk.records_config.update({
    'proxy.config.exec_thread.autoconfig': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.5,
    'proxy.config.exec_thread.limit': 16,
    'proxy.config.accept_threads': 1,
    'proxy.config.task_threads': 2,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'TSEmergency_test'
})

# Load plugin
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'emergency_shutdown.so'), ts)

# www.example.com Host
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'printf "Emergency Shutdown Test"'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
ts.ReturnCode = 33
ts.Ready = 0  # Need this to be 0 because we are testing shutdown, this is to make autest not think ats went away for a bad reason.
ts.Streams.All = Testers.ExcludesExpression('failed to shutdown', 'should NOT contain "failed to shutdown"')
ts.Disk.diags_log.Content = Testers.IncludesExpression('testing emergency shutdown', 'should contain "testing emergency shutdown"')
