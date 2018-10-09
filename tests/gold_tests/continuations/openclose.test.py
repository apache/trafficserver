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
Test transactions and sessions, making sure they open and close in the proper order.
'''

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl needs to be installed on system for this test to work")
)

# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager")

server = Test.MakeOriginServer("server")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: oc.test\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}

Test.PreparePlugin(os.path.join(Test.Variables.AtsTestToolsDir,
                                'plugins', 'ssntxnorder_verify.cc'), ts)

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssntxnorder_verify.*',
    'proxy.config.http.cache.http': 0,  # disable cache to simply the test.
    'proxy.config.cache.enable_read_while_writer': 0
})

ts.Disk.remap_config.AddLine(
    'map http://oc.test:{0} http://127.0.0.1:{1}'.format(
        ts.Variables.port, server.Variables.Port)
)

cmd = 'curl -vs http://127.0.0.1:{0}'.format(ts.Variables.port)
numberOfRequests = 25

tr = Test.AddTestRun()
# Create a bunch of curl commands to be executed in parallel. Default.Process is set in SpawnCommands.
ps = tr.SpawnCommands(cmdstr=cmd,  count=numberOfRequests)
tr.Processes.Default.Env = ts.Env

# Execution order is: ts/server, ps(curl cmds), Default Process.
tr.Processes.Default.StartBefore(
    server, ready=When.PortOpen(server.Variables.Port))
# Adds a delay once the ts port is ready. This is because we cannot test the ts state.
tr.Processes.Default.StartBefore(ts, ready=10)
ts.StartAfter(*ps)
server.StartAfter(*ps)
tr.StillRunningAfter = ts

# Watch the records snapshot file.
records = ts.Disk.File(os.path.join(ts.Variables.RUNTIMEDIR, "records.snap"))

# Check our work on traffic_ctl
# no errors happened,
tr = Test.AddTestRun()
tr.DelayStart = 10
tr.Processes.Default.Command = 'traffic_ctl metric get ssntxnorder_verify.err'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    'ssntxnorder_verify.err 0', 'incorrect statistic return, or possible error.')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

comparator_command = '''
if test "`traffic_ctl metric get ssntxnorder_verify.{0}.start | cut -d ' ' -f 2`" -eq "`traffic_ctl metric get ssntxnorder_verify.{0}.close | cut -d ' ' -f 2`" ; then\
     echo yes;\
    else \
    echo no; \
    fi;
    '''

# number of sessions/transactions opened and closed are equal
tr = Test.AddTestRun()
tr.DelayStart = 10
tr.Processes.Default.Command = comparator_command.format('ssn')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "yes", 'should verify contents')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun()
tr.DelayStart = 10
tr.Processes.Default.Command = comparator_command.format('txn')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "yes", 'should verify contents')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# session count is positive,
tr = Test.AddTestRun()
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl metric get ssntxnorder_verify.ssn.start"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(
    " 0", 'should be nonzero')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# and we receive the same number of transactions as we asked it to make
tr = Test.AddTestRun()
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl metric get ssntxnorder_verify.txn.start"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "ssntxnorder_verify.txn.start {}".format(numberOfRequests), 'should be the number of transactions we made')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
