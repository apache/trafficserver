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

Test.Summary = 'Testing ATS TLS handshake timeout'

ts = Test.MakeATSProcess("ts")

Test.Setup.Copy(os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'timeout', 'ssl-delay-server'))

Test.ContinueOnFail = True
Test.GetTcpPort("block_connect_port")
Test.GetTcpPort("block_ttfb_port")
Test.GetTcpPort("get_block_connect_port")
Test.GetTcpPort("get_block_ttfb_port")

delay_post_connect = Test.Processes.Process(
    "delay post connect", './ssl-delay-server {0} 3 0 server.pem'.format(Test.Variables.block_connect_port))
delay_post_ttfb = Test.Processes.Process(
    "delay post ttfb", './ssl-delay-server {0} 0 6 server.pem'.format(Test.Variables.block_ttfb_port))

delay_get_connect = Test.Processes.Process(
    "delay get connect", './ssl-delay-server {0} 3 0 server.pem'.format(Test.Variables.get_block_connect_port))
delay_get_ttfb = Test.Processes.Process(
    "delay get ttfb", './ssl-delay-server {0} 0 6 server.pem'.format(Test.Variables.get_block_ttfb_port))

ts.Disk.records_config.update(
    {
        'proxy.config.url_remap.remap_required': 1,
        'proxy.config.http.connect_attempts_timeout': 1,
        'proxy.config.http.post_connect_attempts_timeout': 1,
        'proxy.config.http.connect_attempts_max_retries': 1,
        'proxy.config.http.transaction_no_activity_timeout_out': 4,
        'proxy.config.diags.debug.enabled': 0,
        'proxy.config.diags.debug.tags': 'http|ssl',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    })

ts.Disk.remap_config.AddLine('map /connect_blocked https://127.0.0.1:{0}'.format(Test.Variables.block_connect_port))
ts.Disk.remap_config.AddLine('map /ttfb_blocked https://127.0.0.1:{0}'.format(Test.Variables.block_ttfb_port))
ts.Disk.remap_config.AddLine('map /get_connect_blocked https://127.0.0.1:{0}'.format(Test.Variables.get_block_connect_port))
ts.Disk.remap_config.AddLine('map /get_ttfb_blocked https://127.0.0.1:{0}'.format(Test.Variables.get_block_ttfb_port))

# Commenting out the per test case timeouts.  In the CI, there is too big of a risk that we hit those timeouts.  Had hoped to use
# The test case timeouts to differentiate between a good origin timeout and a too long origin timeout

# Run the connection and TTFB timeout tests first with POST.

# Request the port that should timeout in the handshake
# Should retry once
tr = Test.AddTestRun("tr-blocking-post")
tr.Setup.Copy(os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem"))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(delay_post_connect, ready=When.PortOpen(Test.Variables.block_connect_port))
tr.Processes.Default.Command = 'curl -H"Connection:close" -d "bob" -i http://127.0.0.1:{0}/connect_blocked --tlsv1.2'.format(
    ts.Variables.port)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("HTTP/1.1 502 connect failed", "Connect failed")
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = delay_post_connect
tr.StillRunningAfter = Test.Processes.ts

#  Should not catch the connect timeout.  Even though the first bytes are not sent until after the 2 second connect timeout
#  Should not retry the connection
tr = Test.AddTestRun("tr-delayed-post")
tr.Processes.Default.StartBefore(delay_post_ttfb, ready=When.PortOpen(Test.Variables.block_ttfb_port))
tr.Processes.Default.Command = 'curl -H"Connection:close" -d "bob" -i http://127.0.0.1:{0}/ttfb_blocked --tlsv1.2'.format(
    ts.Variables.port)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("504 Connection Timed Out", "Connect timeout")
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = delay_post_ttfb

# Run the connection and TTFB timeout tests again with GET

# Request the port that should timeout in the handshake
# Should retry once
tr = Test.AddTestRun("tr-blocking-get")
tr.Processes.Default.StartBefore(delay_get_connect, ready=When.PortOpen(Test.Variables.get_block_connect_port))
tr.Processes.Default.Command = 'curl -H"Connection:close" -i http://127.0.0.1:{0}/get_connect_blocked --tlsv1.2'.format(
    ts.Variables.port)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("HTTP/1.1 502 connect failed", "Connect failed")
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = delay_get_connect

#  Should not catch the connect timeout.  Even though the first bytes are not sent until after the 2 second connect timeout
#  Since get is idempotent, It will try to connect again even though the GET request had been sent
tr = Test.AddTestRun("tr-delayed-get")
tr.Processes.Default.StartBefore(delay_get_ttfb, ready=When.PortOpen(Test.Variables.get_block_ttfb_port))
tr.Processes.Default.Command = 'curl -H"Connection:close" -i http://127.0.0.1:{0}/get_ttfb_blocked --tlsv1.2'.format(
    ts.Variables.port)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("504 Connection Timed Out", "Connect timeout")
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = delay_get_ttfb

delay_post_connect.Streams.All = Testers.ContainsExpression(
    "Accept try", "Should appear at least two times (may be an extra one due to port ready test)")
delay_post_connect.Streams.All += Testers.ExcludesExpression("TTFB delay", "Should not reach the TTFB delay logic")
delay_post_ttfb.Streams.All = Testers.ContainsExpression("Accept try", "Should appear one time")
delay_post_ttfb.Streams.All += Testers.ContainsExpression("TTFB delay", "Should reach the TTFB delay logic")

delay_get_connect.Streams.All = Testers.ContainsExpression(
    "Accept try", "Should appear at least two times (may be an extra one due to port ready test)")
delay_get_connect.Streams.All += Testers.ExcludesExpression("TTFB delay", "Should not reach the TTFB delay logic")
delay_get_ttfb.Streams.All = Testers.ContainsExpression(
    "Accept try", "Should appear at least two times (may be an extra one due to the port ready test)")
delay_get_ttfb.Streams.All += Testers.ContainsExpression("TTFB delay", "Should reach the TTFB delay logic")
