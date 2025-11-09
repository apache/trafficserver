'''
Test Lua plugin PROXY protocol support
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
Test Lua plugin PROXY protocol API
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)

Test.ContinueOnFail = True

# ---- Setup Origin Server ----
server = Test.MakeOriginServer("server")

request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 4\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "TEST"
}
server.addResponse("sessionfile.log", request_header, response_header)

# ---- Setup ATS ----
ts = Test.MakeATSProcess("ts", enable_proxy_protocol=True)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}/'.format(server.Variables.Port) +
    ' @plugin=tslua.so @pparam=proxy_protocol.lua'
)

ts.Setup.Copy("proxy_protocol.lua", ts.Variables.CONFIGDIR)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ts_lua'
})

# ---- Test with PROXY protocol ----
tr = Test.AddTestRun("Test with PROXY protocol v1")
tr.Processes.Default.Command = (
    f"curl --haproxy-protocol --haproxy-clientip 192.168.1.100 "
    f"http://127.0.0.1:{ts.Variables.proxy_protocol_port}/"
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("TEST", "Response body should be received")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Check debug log for PROXY protocol info
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"PP-Version: 1",
    "PROXY protocol version should be logged"
)
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"PP-Source: 192\.168\.1\.100",
    "PROXY protocol source address should be logged"
)
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"PP-Protocol: 2",
    "PROXY protocol IP family should be logged"
)
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"PP-SocketType: 1",
    "PROXY protocol socket type should be logged"
)

# ---- Test without PROXY protocol ----
tr2 = Test.AddTestRun("Test without PROXY protocol")
tr2.Processes.Default.Command = f"curl http://127.0.0.1:{ts.Variables.port}/"
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression("TEST", "Response body should be received")
tr2.StillRunningAfter = server
tr2.StillRunningAfter = ts

# Check that PP-Not-Present is logged when PROXY protocol is not used
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    r"PP-Not-Present",
    "Should log when PROXY protocol is not present"
)
