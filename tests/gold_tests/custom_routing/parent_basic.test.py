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

sys.path.append(Test.TestDirectory)  # allows importing other modules

import util
import pop

Test.Summary = '''
Basic Parent selection test.
'''

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

util.debugOut('init', origin.Name, origin.Variables.Port)
util.debugOut('init', dns.Name, dns.Variables.Port)

# configure edge to shard to mid
pop.cfgParents(edge, mid, 'parent.test')

routing_logs = pop.enableRoutingLog(edge + mid)

all_proc = edge + mid + [dns, origin]

pop.cfgResponse(origin, dns, 'direct.test', '/basic.txt', "basic hello")
pop.cfgResponse(origin, dns, 'parent.test', '/basic.txt', "basic parent")

# START ROUTING TESTS

# Basic Parent Test
tr = Test.AddTestRun("Test traffic server started properly")
p = tr.Processes.Default
p.Command = "curl --proxy 127.0.0.1:{} http://direct.test/basic.txt".format(edge[0].Variables.Port) + \
    "; curl --proxy 127.0.0.1:{} http://parent.test/basic.txt".format(edge[0].Variables.Port)
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
cmd_text = 'cat {log}'.format(log=routing_logs[0])
for p in all_proc:
    cmd_text += ' | sed "s/127.0.0.1:{port}/{host}/g"'.format(port=p.Variables.Port, host=p.Name)
cmd_text += ' > {log}.sub'.format(log=routing_logs[0])
print(cmd_text)
tr.Processes.Default.Command = cmd_text
log = tr.Disk.File(routing_logs[0] + '.sub')


util.logContains(log, ['direct.test', 'nh=origin', 'code=200'])
