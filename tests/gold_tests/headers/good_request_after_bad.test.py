'''
Verify that request following a ill-formed request is not processed
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
Verify that request following a ill-formed request is not processed
'''
Test.ContinueOnFail = True
ts = Test.MakeATSProcess("ts", enable_cache=True)

ts.Disk.records_config.update({'proxy.config.diags.debug.tags': 'http',
                               'proxy.config.diags.debug.enabled': 0,
                               })

Test.ContinueOnFail = True
server = Test.MakeOriginServer("server")
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nLast-Modified: Tue, 08 May 2018 15:49:41 GMT\r\nCache-Control: max-age=1000\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# Make a good request to get item in the cache for later tests
tr = Test.AddTestRun("Good control")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'printf "GET / HTTP/1.1\r\nHost: bob\r\n\r\n" | nc  127.0.0.1 {}'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("space after header name")
tr.Processes.Default.Command = 'printf "GET / HTTP/1.1\r\nHost : bob\r\n\r\nGET / HTTP/1.1\r\nHost: boa\r\n\r\n" | nc  127.0.0.1 {}'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = 'gold/bad_good_request.gold'

tr = Test.AddTestRun("Bad protocol number")
tr.Processes.Default.Command = 'printf "GET / HTTP/11.1\r\nhost: bob\r\n\r\nGET / HTTP/1.1\r\nHost: boa\r\n\r\n" | nc  127.0.0.1 {}'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = 'gold/bad_protocol_number.gold'

tr = Test.AddTestRun("Unsupported Transfer Encoding value")
tr.Processes.Default.Command = 'printf "GET / HTTP/1.1\r\nhost: bob\r\ntransfer-encoding: random\r\n\r\nGET / HTTP/1.1\r\nHost: boa\r\n\r\n" | nc  127.0.0.1 {}'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = 'gold/bad_te_value.gold'

# TRACE request with a body
tr = Test.AddTestRun("Trace request with a body")
tr.Processes.Default.Command = 'printf "TRACE /foo HTTP/1.1\r\nHost: bob\r\nContent-length:2\r\n\r\nokGET / HTTP/1.1\r\nHost: boa\r\n\r\n" | nc  127.0.0.1 {}'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = 'gold/bad_good_request.gold'

tr = Test.AddTestRun("Trace request with a chunked body")
tr.Processes.Default.Command = 'printf "TRACE /foo HTTP/1.1\r\nHost: bob\r\ntransfer-encoding: chunked\r\n\r\n2\r\nokGGET / HTTP/1.1\r\nHost: boa\r\n\r\n" | nc  127.0.0.1 {}'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = 'gold/bad_good_request.gold'

tr = Test.AddTestRun("Trace request with a chunked body via curl")
tr.Processes.Default.Command = 'curl -v --http1.1 --header "Transfer-Encoding: chunked" -d aaa -X TRACE -k http://127.0.0.1:{}/foo'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = 'gold/bad_good_request.gold'

tr = Test.AddTestRun("Trace request via curl")
tr.Processes.Default.Command = 'curl -v --http1.1 -X TRACE -k http://127.0.0.1:{}/bar'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    r"HTTP/1.1 501 Unsupported method \('TRACE'\)",
    "microserver does not support TRACE")

# Methods are case sensitive. Verify that "gET" is not confused with "GET".
tr = Test.AddTestRun("mixed case method")
tr.Processes.Default.Command = 'printf "gET / HTTP/1.1\r\nHost:bob\r\n\r\nGET / HTTP/1.1\r\nHost: boa\r\n\r\n" | nc  127.0.0.1 {}'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = 'gold/bad_method.gold'
