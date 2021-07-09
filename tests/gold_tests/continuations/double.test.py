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
Test transactions and sessions for http1, making sure the two continuations catch the same number of hooks.
'''

Test.ContinueOnFail = True
# Define default ATS. Disable the cache to simplify the test.
ts = Test.MakeATSProcess("ts", select_ports=True, command="traffic_server", enable_cache=False, dump_runroot=True)
server = Test.MakeOriginServer("server")

# Set TS_RUNROOT, traffic_ctl needs it to find the socket.
ts.SetRunRootEnv()

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: double_h2.test\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length:0\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)

# add port and remap rule
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'continuations_verify.*',
})

# add plugin to assist with test metrics
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir,
                                    'continuations_verify.so'), ts)

comparator_command = '''
if test "`traffic_ctl metric get continuations_verify.{0}.close.1 | cut -d ' ' -f 2`" -eq "`traffic_ctl metric get continuations_verify.{0}.close.2 | cut -d ' ' -f 2`" ; then\
     echo yes;\
    else \
    echo no; \
    fi; \
    traffic_ctl metric match continuations_verify
    '''

cmd = 'curl -vs http://127.0.0.1:{0}/'.format(ts.Variables.port)
numberOfRequests = 55

tr = Test.AddTestRun()

# Create a bunch of curl commands to be executed in parallel. Default.Process is set in SpawnCommands.
# On Fedora 28/29, it seems that curl will occasionally timeout after a couple seconds and return exitcode 2
# Examining the packet capture shows that Traffic Server dutifully sends the response
ps = tr.SpawnCommands(cmdstr=cmd, count=numberOfRequests, retcode=Any(0, 2))
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = Any(0, 2)

# Execution order is: ts/server, ps(curl cmds), Default Process.
tr.Processes.Default.StartBefore(
    server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
ts.StartAfter(*ps)
server.StartAfter(*ps)
tr.StillRunningAfter = ts

# Signal that all the curl processes have completed and poll for done metric
tr = Test.AddTestRun("Curl Done")
tr.Processes.Default.Command = (
    "traffic_ctl plugin msg done done ; "
    "N=60 ; "
    "while (( N > 0 )) ; "
    "do "
    "sleep 1 ; "
    'if [[ "$$( traffic_ctl metric get continuations_verify.test.done )" = '
    '"continuations_verify.test.done 1" ]] ; then exit 0 ; '
    "fi ; "
    "let N=N-1 ; "
    "done ; "
    "echo TIMEOUT ; "
    "exit 1"
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# number of sessions/transactions opened and closed are equal
tr = Test.AddTestRun("Check Ssn")
tr.Processes.Default.Command = comparator_command.format('ssn')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("yes", 'should verify contents')
# Session and Txn's should be non-zero
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("continations_verify.ssn.close.1 0", 'should be nonzero')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("continations_verify.ssn.close.2 0", 'should be nonzero')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("continations_verify.txn.close.1 0", 'should be nonzero')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("continations_verify.txn.close.2 0", 'should be nonzero')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "continuations_verify.txn.close.1 {}".format(numberOfRequests), 'should be the number of transactions we made')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "continuations_verify.txn.close.2 {}".format(numberOfRequests), 'should be the number of transactions we made')
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Check Txn")
tr.Processes.Default.Command = comparator_command.format('txn')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("yes", 'should verify contents')
tr.StillRunningAfter = ts
