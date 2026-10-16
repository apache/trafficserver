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
"""Verify a locally half-closed origin session cannot reenter the pool."""
import os
import sys
from ports import get_port

Test.Summary = "Evict half-closed HTTP/2 origin sessions, including after stream release."
tr = Test.AddTestRun("Keep a draining origin alive across two subsequent requests")
tr.Setup.Copy("half_close_probe.py")
tr.Setup.Copy("origin_lifecycle.py")
stop = f"{tr.RunDirectory}/stop"
lifecycle_script = f"{tr.RunDirectory}/origin_lifecycle.py"
server = Test.Processes.Process("origin")
port = get_port(server, "https_port")
for name in ("server.pem", "server.key"):
    server.Setup.Copy(os.path.join(Test.Variables.AtsTestToolsDir, "ssl", name))
server.Command = f"{sys.executable} {tr.RunDirectory}/half_close_probe.py origin {port} server.pem server.key {stop}"
server.Ready = When.PortOpen(port)
server.ReturnCode = 0
server.Streams.stdout += Testers.ContainsExpression(
    "half_close sessions=2 requests=3", "The healthy session must serve both later requests.")
ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update(
    {
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 1,
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.http.server_session_sharing.match': 'ip',
        'proxy.config.http.transaction_no_activity_timeout_out': 2,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http2|http_ss',
    })
ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{port}')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    "half_close state", "Draining sessions must not be discovered in the pool.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"{sys.executable} {tr.RunDirectory}/half_close_probe.py client {ts.Variables.port} {stop} {ts.Disk.traffic_out.AbsPath}"
tr.Processes.Default.ReturnCode = 0
tr.TimeOut = 30
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr = Test.AddTestRun("Validate origin counts and stop")
tr.Processes.Default.Command = f"{sys.executable} {lifecycle_script} {stop}"
tr.Processes.Default.ReturnCode = 0
tr.TimeOut = 15
