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

Test.Summary = 'Testing ATS TLS handshake timeout'

ts = Test.MakeATSProcess("ts")

server2 = Test.MakeOriginServer("server2")

Test.Setup.Copy('ssl-delay-server')

Test.ContinueOnFail = True
Test.GetTcpPort("block_connect_port")
Test.GetTcpPort("block_ttfb_port")
Test.GetTcpPort("get_block_connect_port")
Test.GetTcpPort("get_block_ttfb_port")

ts.Disk.records_config.update({
    'proxy.config.url_remap.remap_required': 1,
    'proxy.config.http.connect_attempts_timeout': 1,
    'proxy.config.http.post_connect_attempts_timeout': 1,
    'proxy.config.http.connect_attempts_max_retries': 1,
    'proxy.config.http.transaction_no_activity_timeout_out': 4,
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http|ssl',
})

ts.Disk.remap_config.AddLine('map /connect_blocked https://127.0.0.1:{0}'.format(Test.Variables.block_connect_port))
ts.Disk.remap_config.AddLine('map /ttfb_blocked https://127.0.0.1:{0}'.format(Test.Variables.block_ttfb_port))
ts.Disk.remap_config.AddLine('map /get_connect_blocked https://127.0.0.1:{0}'.format(Test.Variables.get_block_connect_port))
ts.Disk.remap_config.AddLine('map /get_ttfb_blocked https://127.0.0.1:{0}'.format(Test.Variables.get_block_ttfb_port))

# Run the connection and TTFB timeout tests first with POST.

# Request the port that should timeout in the handshake
# Should retry once
tr = Test.AddTestRun("tr-blocking")
tr.Setup.Copy("case2.sh")
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
tr.Setup.Copy("../chunked_encoding/ssl/server.pem")
tr.Processes.Default.Command = 'sh ./case2.sh {0} 3 0 {1} connect_blocked'.format(Test.Variables.block_connect_port, ts.Variables.port)
tr.Processes.Default.TimeOut = 6
tr.Processes.Default.Streams.All = Testers.ContainsExpression("HTTP/1.1 502 internal error - server connection terminated", "Connect failed")

#  Should not catch the connect timeout.  Even though the first bytes are not sent until after the 2 second connect timeout
#  Shoudl not retry the connection
tr = Test.AddTestRun("tr-delayed")
tr.Processes.Default.Command = 'sh ./case2.sh {0} 0 6 {1} ttfb_blocked'.format(Test.Variables.block_ttfb_port, ts.Variables.port)
tr.Processes.Default.TimeOut = 15
tr.Processes.Default.Streams.All = Testers.ContainsExpression("504 Connection Timed Out", "Conntect timeout")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("tr-connect-retry")
tr.Processes.Default.Command = 'grep "Accept try" server{0}post.log  | wc -l'.format(Test.Variables.block_connect_port)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("^2$", "Two accept tries")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("tr-post-ttfb-retry")
tr.Processes.Default.Command = 'grep "Accept try" server{0}post.log  | wc -l'.format(Test.Variables.block_ttfb_port)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("^1$", "Only 1 accept try")
tr.Processes.Default.ReturnCode = 0

# Run the connection and TTFB timeout tests again with GET

# Request the port that should timeout in the handshake
# Should retry once
tr = Test.AddTestRun("tr-blocking")
tr.Processes.Default.Command = 'sh ./case2.sh {0} 3 0 {1} get_connect_blocked get'.format(Test.Variables.get_block_connect_port, ts.Variables.port)
tr.Processes.Default.TimeOut = 6
tr.Processes.Default.Streams.All = Testers.ContainsExpression("HTTP/1.1 502 internal error - server connection terminated", "Connect failed")

#  Should not catch the connect timeout.  Even though the first bytes are not sent until after the 2 second connect timeout
#  Since get is idempotent, It will try to connect again even though the GET request had been sent
tr = Test.AddTestRun("tr-delayed")
tr.Processes.Default.Command = 'sh ./case2.sh {0} 0 6 {1} get_ttfb_blocked get'.format(Test.Variables.get_block_ttfb_port, ts.Variables.port)
tr.Processes.Default.TimeOut = 15
tr.Processes.Default.Streams.All = Testers.ContainsExpression("504 Connection Timed Out", "Conntect timeout")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("tr-connect-retry")
tr.Processes.Default.Command = 'grep "Accept try" server{0}get.log  | wc -l'.format(Test.Variables.get_block_connect_port)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("^2$", "Two accept tries")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("tr-get-ttfb-retry")
tr.Processes.Default.Command = 'grep "Accept try" server{0}get.log  | wc -l'.format(Test.Variables.get_block_ttfb_port)
tr.Processes.Default.Streams.All = Testers.ContainsExpression("^2$", "Two accept tries")
tr.Processes.Default.ReturnCode = 0

