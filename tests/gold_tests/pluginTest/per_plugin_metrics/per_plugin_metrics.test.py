'''
Verify the per-plugin workload counters proxy.process.plugin.<name>.{invocations,bytes,transfers}.
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
Verify the per-plugin workload counters proxy.process.plugin.<name>.invocations (global and remap
dispatch), .bytes and .transfers (PluginVC intercept transport).
'''

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: test.example\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

# header_rewrite (global) and conf_remap (remap) exercise the two invocation-counter sites;
# generator serves its body through a PluginVC intercept, exercising the bytes and transfers counters.
ts.Setup.CopyAs('rules/global.conf', Test.RunDirectory)
ts.Disk.plugin_config.AddLine(f'header_rewrite.so {Test.RunDirectory}/global.conf')
ts.Disk.remap_config.AddLine(
    f'map http://test.example http://127.0.0.1:{server.Variables.Port} '
    f'@plugin=conf_remap.so @pparam=proxy.config.url_remap.pristine_host_hdr=1')
ts.Disk.remap_config.AddLine('map http://gen.example http://127.0.0.1/ @plugin=generator.so')

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'plugin',
})

# curl as a forward proxy sends an absolute-URI request so the named remap rule (conf_remap) matches;
# the 200 confirms it was served rather than 404'd.
tr = Test.AddTestRun("Drive traffic through global + remap plugins")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.MakeCurlCommand(f'-s -D - -o /dev/null --proxy 127.0.0.1:{ts.Variables.port} "http://test.example/"', ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('200 OK', 'request should match the remap rule and be served')
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Drive a generator request: it serves a 4096-byte body through a PluginVC intercept.
tr = Test.AddTestRun("Drive traffic through a PluginVC intercept plugin")
tr.MakeCurlCommand(f'-s -o /dev/null --proxy 127.0.0.1:{ts.Variables.port} "http://gen.example/nocache/4096"', ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("Global plugin invocation counter is non-zero")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'traffic_ctl metric get proxy.process.plugin.header_rewrite.invocations'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'proxy\.process\.plugin\.header_rewrite\.invocations [1-9]', 'global header_rewrite invocations should be counted')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("Remap plugin invocation counter is non-zero")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'traffic_ctl metric get proxy.process.plugin.conf_remap.invocations'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'proxy\.process\.plugin\.conf_remap\.invocations [1-9]', 'remap conf_remap invocations should be counted')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("PluginVC intercept bytes counter is non-zero")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'traffic_ctl metric get proxy.process.plugin.generator.bytes'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'proxy\.process\.plugin\.generator\.bytes [1-9]', 'generator PluginVC transport bytes should be counted')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("PluginVC intercept transfers counter is non-zero")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'traffic_ctl metric get proxy.process.plugin.generator.transfers'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    r'proxy\.process\.plugin\.generator\.transfers [1-9]', 'generator PluginVC transfer events should be counted')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
