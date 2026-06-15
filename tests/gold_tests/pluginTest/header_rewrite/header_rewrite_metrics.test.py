'''
Verify header_rewrite work counters proxy.process.plugin.header_rewrite.{operators,conditions}.
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

Test.Summary = '''
Verify header_rewrite increments proxy.process.plugin.header_rewrite.operators per operator
executed and .conditions per condition evaluated.
'''

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: test.example\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

# The global ruleset evaluates one condition and runs two operators per response.
ts.Setup.CopyAs('rules/metrics.conf', Test.RunDirectory)
ts.Disk.plugin_config.AddLine(f'header_rewrite.so {Test.RunDirectory}/metrics.conf')
ts.Disk.remap_config.AddLine(f'map http://test.example http://127.0.0.1:{server.Variables.Port}')

ts.Disk.records_config.update(
    {
        'proxy.config.url_remap.pristine_host_hdr': 1,
        'proxy.config.diags.debug.enabled': 0,
        'proxy.config.diags.debug.tags': 'header_rewrite',
    })

tr = Test.AddTestRun("Drive a transaction through the global ruleset")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.MakeCurlCommand(f'-s -D - -o /dev/null --proxy 127.0.0.1:{ts.Variables.port} "http://test.example/"', ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('200 OK', 'request should be served')
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("operators-executed counter is non-zero")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'traffic_ctl metric get proxy.process.plugin.header_rewrite.operators'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'proxy\.process\.plugin\.header_rewrite\.operators [1-9]', 'header_rewrite operators executed should be counted')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("conditions-evaluated counter is non-zero")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'traffic_ctl metric get proxy.process.plugin.header_rewrite.conditions'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'proxy\.process\.plugin\.header_rewrite\.conditions [1-9]', 'header_rewrite conditions evaluated should be counted')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
