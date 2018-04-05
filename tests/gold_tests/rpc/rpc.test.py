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
Tests various command line tools that use the rpc interface. 
'''

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
})

tr = Test.AddTestRun()
tr.Processes.Default.Command =  "curl http://127.0.0.1:{0}".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.StartBefore(ts, ready=When.PortOpen(ts.Variables.port))
tr.StillRunningAfter = ts

# test record match 
tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl config match ssl"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.IncludesExpression("proxy.config.ssl.enabled", 'did not get records')
tr.StillRunningAfter = ts

# test record describe
tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl config describe proxy.config.diags.debug.enabled"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.IncludesExpression("Name            : proxy.config.diags.debug.enabled", 'error config describe')
tr.StillRunningAfter = ts

# test setting a config. would never be 3 normally. 
tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl config set proxy.config.diags.debug.enabled 3"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.IncludesExpression("set proxy.config.diags.debug.enabled", 'error setting config')
tr.StillRunningAfter = ts

tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl config match proxy.config.diags.debug.enabled"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.IncludesExpression("proxy.config.diags.debug.enabled: 3", 'error config did not get set')
tr.StillRunningAfter = ts

# set debug.enabled back to 1
tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl config set proxy.config.diags.debug.enabled 1"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.IncludesExpression("set proxy.config.diags.debug.enabled", 'error config did not get set')
tr.StillRunningAfter = ts

# test out alarms
tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl alarm list"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Error", 'error could not get alarms')
tr.StillRunningAfter = ts

# test server status
tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl server status"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.IncludesExpression("Proxy -- on", 'error could not get server status')
tr.StillRunningAfter = ts

# following tests have no return values. mainly to test that there are no crashes and the rpc api calls don't hang waiting on a response
tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl server drain"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

tr = Test.AddTestRun()
tr.Processes.Default.Command = "traffic_ctl plugin msg tag hello"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts
