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

Test.Summary = 'Testing ATS client inactivity timeout'

Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

Test.ContinueOnFail = True

Test.GetTcpPort("upstream_port1")
Test.GetTcpPort("upstream_port2")
Test.GetTcpPort("upstream_port3")
Test.GetTcpPort("upstream_port4")
Test.GetTcpPort("upstream_port5")
Test.GetTcpPort("upstream_port6")

ts.addSSLfile("../tls/ssl/server.pem")
ts.addSSLfile("../tls/ssl/server.key")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.url_remap.remap_required': 1,
    'proxy.config.http.transaction_no_activity_timeout_in': 2,
})

ts.Disk.remap_config.AddLines([
    'map /case1 http://127.0.0.1:{0}'.format(Test.Variables.upstream_port1),
    'map /case2 http://127.0.0.1:{0}'.format(Test.Variables.upstream_port2),
    'map /case3 http://127.0.0.1:{0}'.format(Test.Variables.upstream_port3),
    'map /case4 http://127.0.0.1:{0}'.format(Test.Variables.upstream_port4),
    'map /case5 http://127.0.0.1:{0}'.format(Test.Variables.upstream_port5),
    'map /case6 http://127.0.0.1:{0}'.format(Test.Variables.upstream_port6),
])

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Using netcat with explicit delays instead of the delay option with microserver because it appears
# that microserver will not delay responses to POST requests
# The delay-inactive-server.sh will deplay for 4 seconds before returning a response. This is more
# than the 2 second proxy.config.http.transaction_no_activity_timeout_in
# These tests exercise that the client inactivity timeout is disabled after the request and post body
# are sent.  So a slow to respond server will not trigger the client inactivity timeout.

tr4 = Test.AddTestRun("tr")
tr4.Processes.Default.StartBefore(ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr4.Setup.Copy('delay-inactive-server.sh')
tr4.Setup.Copy('case-inactive4.sh')
tr4.Processes.Default.ReturnCode = 0
tr4.Processes.Default.Command = 'sh -x ./case-inactive4.sh {0} {1} case4'.format(
    ts.Variables.ssl_port, Test.Variables.upstream_port4)
tr4.Processes.Default.Streams.All = Testers.ContainsExpression("HTTP/2 200", "Should get successful response")

tr = Test.AddTestRun("tr")
tr.Setup.Copy('case-inactive1.sh')
tr.Processes.Default.Command = 'sh -x ./case-inactive1.sh {0} {1} case1'.format(ts.Variables.port, Test.Variables.upstream_port1)
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 200", "Client inactivity should not trigger during server stall")

tr2 = Test.AddTestRun("tr")
tr2.Setup.Copy('case-inactive2.sh')
tr2.Processes.Default.Command = 'sh -x ./case-inactive2.sh {0} {1} case2'.format(
    ts.Variables.ssl_port, Test.Variables.upstream_port2)
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 200", "Client inactivity should not trigger during server stall")

tr3 = Test.AddTestRun("tr")
tr3.Setup.Copy('case-inactive3.sh')
tr3.Processes.Default.Command = 'sh -x ./case-inactive3.sh {0} {1} case3'.format(
    ts.Variables.ssl_port, Test.Variables.upstream_port3)
tr3.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/2 200", "Client inactivity should not trigger during server stall")

tr5 = Test.AddTestRun("tr")
tr5.Setup.Copy('case-inactive5.sh')
tr5.Processes.Default.Command = 'sh -x ./case-inactive5.sh {0} {1} case5'.format(ts.Variables.port, Test.Variables.upstream_port5)
tr5.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 200", "Client inactivity timeout should not apply during server stall")

tr6 = Test.AddTestRun("tr")
tr6.Setup.Copy('case-inactive6.sh')
tr6.Processes.Default.Command = 'sh -x ./case-inactive6.sh {0} {1} case6'.format(
    ts.Variables.ssl_port, Test.Variables.upstream_port6)
tr6.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "HTTP/1.1 200", "Client inactivity timeout should not apply during server stall")
