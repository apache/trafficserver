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
import subprocess

Test.Summary = '''
Test new log field giving line number in config file of rule used to remap URL.
'''

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)
Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts", select_ports=False)

ts.Disk.records_config.update({
    # 'proxy.config.diags.debug.enabled': 1,
    'proxy.config.http.server_ports': 'ipv4:{0}'.format(ts.Variables.port)
})

ts.Disk.remap_config.AddLine(
    'map http://127.0.0.1:{0} http://httpbin.org/ip'.format(ts.Variables.port)
)

ts.Disk.remap_config.AddLine('')
ts.Disk.remap_config.AddLine('')

ts.Disk.remap_config.AddLine(
    'map http://127.0.0.3:{0} http://httpbin.org/ip'.format(ts.Variables.port)
)

ts.Disk.remap_config.AddLine('')

ts.Disk.remap_config.AddLine(
    'map http://127.0.0.2:{0} http://httpbin.org/ip'.format(ts.Variables.port)
)

ts.Disk.logging_config.AddLines(
    '''custom = format {
  Format = "%<rcfln>"
}

log.ascii {
  Format = custom,
  Filename = 'test_rcfln'
}'''.split("\n")
)

Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'test_rcfln.log'),
               exists=True, content='gold/test_rcfln.gold')

# Ask the OS if the port is ready for connect()
#
def CheckPort(Port):
    return lambda: 0 == subprocess.call('netstat --listen --tcp -n | grep -q :{}'.format(Port), shell=True)

tr = Test.AddTestRun()
# Delay on readiness of port
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=CheckPort(ts.Variables.port))
#
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.2:{0}" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

# Unremapped URL
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.4:{0}" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.3:{0}" --verbose'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

# Delay to give ATS time to generate log entries.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'echo WAIT'
tr.DelayStart = 10
tr.Processes.Default.ReturnCode = 0
