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
import socket
import re

Test.Summary = '''
Test ACL options on remap.
'''

SERVER_PORT = 7000

def IsNonRoutableAddr(addr) :
    return addr.startswith('192.168') or re.match('172[.]1[6789][.]', addr) or re.match('172[.]2[0-9][.]', addr) or re.match('172[.]3[01][.]', addr) or addr.startswith('10.')

def IsLoopbackAddr(addr) :
    return addr.startswith('127')

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)

Test.ContinueOnFail = True

Test.testName = "Remap ACL"
ts = Test.MakeATSProcess("ts")
www = Test.Processes.Process('www', 'python3 ' + os.path.join(Test.Variables.AtsTestToolsDir, 'min-www.py'))
probe = '{}'.format(os.path.join(Test.Variables.AtsTestToolsDir, 'http-check-status.py'))

ts.Disk.remap_config.AddLine(
    'map http://test1.com http://127.0.0.1:{0}'.format(SERVER_PORT)
)
ts.Disk.remap_config.AddLine(
    'map http://test2.com http://127.0.0.1:{0} @src_ip=127.0.0.0-127.255.255.255 @action=deny'.format(SERVER_PORT)
)
ts.Disk.remap_config.AddLine(
    'map http://test3.com http://127.0.0.1:{0} @method=DELETE @method=HEAD @src_ip=192.168.0.0-192.168.255.255 @src_ip=172.16.0.0-172.31.255.255 @src_ip=10.0.0.0-10.255.255.255 @action=allow'.format(SERVER_PORT)
)

addrs = socket.getaddrinfo(socket.getfqdn(), 0, socket.AF_INET)
routable_addr = None
nonroutable_addr = None
for data in addrs :
    addr = data[4][0];
    if IsLoopbackAddr(addr) :
        pass
    elif IsNonRoutableAddr(addr) :
        if not nonroutable_addr :
            nonroutable_addr = addr
    else :
        if not routable_addr :
            routable_addr = addr
local_addr = '127.0.0.1'
if routable_addr :
    local_addr = routable_addr
elif nonroutable_addr :
    local_addr = nonroutable_addr

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python {0} --local-proxy {1} http://test1.com'.format(probe, ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(Test.Processes.www, ready = When.PortOpen(SERVER_PORT))
tr.Processes.Default.Streams.stdout = "gold/200.gold"

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python {0} --local-proxy {1} http://test2.com'.format(probe, ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/403.gold"

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python {0} --local-proxy {1} --bind {2} http://test3.com'.format(probe, ts.Variables.port, local_addr)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/403.gold"

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'python {0} --local-proxy {1} --bind {2} --method HEAD http://test3.com'.format(probe, ts.Variables.port, local_addr)
tr.Processes.Default.ReturnCode = 0
if IsNonRoutableAddr(local_addr) :
    tr.Processes.Default.Streams.stdout = "gold/200.gold"
else :
    tr.Processes.Default.Streams.stdout = "gold/403.gold"
