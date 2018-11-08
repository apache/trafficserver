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

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl needs to be installed on system for this test to work"),
)
Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False, command="traffic_manager")
server = Test.MakeOriginServer("server")
server2 = Test.MakeOriginServer("server2")

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
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.cache.http': 0,  # disable cache to simply the test.
    'proxy.config.cache.enable_read_while_writer': 0,
    'proxy.config.ssl.client.verify.server':  0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.http2.max_concurrent_streams_in': 65535
})

# add plugin to assist with test metrics
Test.PreparePlugin(os.path.join(Test.Variables.AtsTestToolsDir,
                                 'plugins', 'continuations_verify.cc'), ts)

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
# On Fedora 28/29, it seems that curl will occaisionally timeout after a couple seconds and return exitcode 2
# Examinig the packet capture shows that Traffic Server dutifully sends the response
ps = tr.SpawnCommands(cmdstr=cmd, count=numberOfRequests, retcode=Any(0,2))
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = Any(0,2)

# Execution order is: ts/server, ps(curl cmds), Default Process.
tr.Processes.Default.StartBefore(
    server, ready=When.PortOpen(server.Variables.Port))
# Adds a delay once the ts port is ready. This is because we cannot test the ts state.
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
ts.StartAfter(*ps)
server.StartAfter(*ps)
tr.StillRunningAfter = ts

# Signal that all the curl processes have completed
tr = Test.AddTestRun("Curl Done")
tr.Processes.Default.Command = "traffic_ctl plugin msg done done"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# Parking this as a ready tester on a meaningless process
# To stall the test runs that check for the stats until the
# stats have propagated and are ready to read.
def make_done_stat_ready(tsenv): 
  def done_stat_ready(process, hasRunFor, **kw):
    retval = subprocess.run("traffic_ctl metric get continuations_verify.test.done > done  2> /dev/null", shell=True, env=tsenv)
    if retval.returncode == 0:
      retval = subprocess.run("grep 1 done > /dev/null", shell = True, env=tsenv)
    return retval.returncode == 0

  return done_stat_ready
  
# number of sessions/transactions opened and closed are equal
tr = Test.AddTestRun("Check Ssn")
server2.StartupTimeout = 60
# Again, here the imporant thing is the ready function not the server2 process
tr.Processes.Default.StartBefore(server2, ready=make_done_stat_ready(ts.Env))
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
tr.StillRunningAfter = server2

tr = Test.AddTestRun("Check Txn")
tr.Processes.Default.Command = comparator_command.format('txn')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("yes", 'should verify contents')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server2

