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
import util
import pop
Test.Summary = '''
Basic Parent selection test.
'''


sys.path += [Test.TestDirectory]  # allows importing other modules

pop.Test = Test
pop.Testers = Testers
pop.When = When

util.Test = Test
util.Testers = Testers

# Needs Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "curl needs to be installed on system for this test to work"),
    Condition.HasProgram("nc", "nc needs to be installed on system for this test to work")
)
Test.ContinueOnFail = False

# Make a fake origin server
origin = Test.MakeOriginServer("origin")

# Define multiple ATS nodes, per pop
num_hosts = 2
edge = pop.makeATS(['edge' + str(i) for i in range(num_hosts)], origin)
mid = pop.makeATS(['mid' + str(i) for i in range(num_hosts)], origin)

# setup dns for all ats servers
dns = Test.MakeDNServer("dns")
pop.cfgDNS(edge + mid, dns)

# configure edge to shard to mid
pop.cfgParents(edge, mid)

routing_logs = pop.enableRoutingLog(edge + mid)

all_proc = edge + mid + [dns, origin]

pop.cfgResponse(origin, dns, 'parent.test', '/basic.txt', "hello")

# START ROUTING TESTS

# Basic Parent Test
tr = Test.AddTestRun("Test traffic server started properly")
p = tr.Processes.Default
p.Command = "curl --proxy 127.0.0.1:{} http://parent.test/basic.txt".format(edge[0].Variables.port)
#p.Command += '; sleep 2; cat '+routing_logs[0]
p.ReturnCode = 0
pop.waitAllStarted(tr, all_proc)
p.StillRunningAfter = all_proc

# END ROUTING TESTS
tr = pop.checkAllProcessRunning(all_proc)


# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
tr = Test.AddTestRun('poll logs')
ts = edge[0]
cmd_text = os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' + routing_logs[0]
print(cmd_text)
tr.Processes.Default.Command = cmd_text
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("parse logs")
log = tr.Disk.File(routing_logs[0])

util.logContains(log, "parent")
