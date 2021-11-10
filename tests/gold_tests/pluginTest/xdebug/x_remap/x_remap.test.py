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

import sys

Test.Summary = '''
Test xdebug plugin X-Remap, Probe and fwd headers
'''

server = Test.MakeOriginServer("server", options={'--load': (Test.TestDirectory + '/x_remap-observer.py')})

request_header = {
    "headers": "GET /argh HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({
    'proxy.config.url_remap.remap_required': 0,
    'proxy.config.diags.debug.enabled': 0,
    # 'proxy.config.diags.debug.tags': 'http|xdebug'
    # 'proxy.config.diags.debug.tags': 'xdebug'
})

ts.Disk.plugin_config.AddLine('xdebug.so')

ts.Disk.remap_config.AddLine(
    "map http://one http://127.0.0.1:{0}".format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    "map http://two http://127.0.0.1:{0}".format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    "regex_map http://three[0-9]+ http://127.0.0.1:{0}".format(server.Variables.Port)
)

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(Test.Processes.server)
tr.Processes.Default.Command = "cp {}/tcp_client.py {}/tcp_client.py".format(
    Test.Variables.AtsTestToolsDir, Test.RunDirectory)
tr.Processes.Default.ReturnCode = 0


def sendMsg(msgFile):

    tr = Test.AddTestRun()
    tr.Processes.Default.Command = (
        f"( {sys.executable} {Test.RunDirectory}/tcp_client.py 127.0.0.1 {ts.Variables.port} {Test.TestDirectory}/{msgFile}.in"
        f" ; echo '======' ) | sed 's/:{server.Variables.Port}/:SERVER_PORT/' >>  {Test.RunDirectory}/out.log 2>&1 "
    )
    tr.Processes.Default.ReturnCode = 0


sendMsg('none')
sendMsg('one')
sendMsg('two')
sendMsg('three')
sendMsg('four')
sendMsg('fwd1')
sendMsg('fwd2')
sendMsg('fwd3')
sendMsg('fwd4')
sendMsg('fwd5')

tr = Test.AddTestRun()
tr.Processes.Default.Command = "echo test out.gold"
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File("out.log")
f.Content = "out.gold"

tr = Test.AddTestRun()
tr.Processes.Default.Command = "echo test x_remap.gold"
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File("x_remap.log")
f.Content = "x_remap.gold"
