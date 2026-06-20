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

import re

Test.Summary = '''
Verify the per-plugin workload counters proxy.process.plugin.<name>.invocations (global and remap
dispatch), .bytes and .transfers (PluginVC intercept transport).
'''

Test.SkipUnless(
    Condition.PluginExists('header_rewrite.so'), Condition.PluginExists('conf_remap.so'), Condition.PluginExists('generator.so'))

Test.ContinueOnFail = True


class TestPerPluginWorkloadCounters:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")
        request_header = {"headers": "GET / HTTP/1.1\r\nHost: test.example\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        self.server.addResponse("sessionfile.log", request_header, response_header)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")

        # header_rewrite (global) and conf_remap (remap) exercise the two invocation-counter sites;
        # generator serves its body through a PluginVC intercept, exercising the bytes and transfers counters.
        self.ts.Setup.CopyAs('rules/global.conf', Test.RunDirectory)
        self.ts.Disk.plugin_config.AddLine(f'header_rewrite.so {Test.RunDirectory}/global.conf')
        self.ts.Disk.remap_config.AddLine(
            f'map http://test.example http://127.0.0.1:{self.server.Variables.Port} '
            f'@plugin=conf_remap.so @pparam=proxy.config.url_remap.pristine_host_hdr=1')
        self.ts.Disk.remap_config.AddLine('map http://gen.example http://127.0.0.1/ @plugin=generator.so')

        self.ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 0,
            'proxy.config.diags.debug.tags': 'plugin',
        })

    def driveTraffic(self):
        # curl as a forward proxy sends an absolute-URI request so the named remap rule (conf_remap)
        # matches; the 200 confirms it was served rather than 404'd.
        tr = Test.AddTestRun("Drive traffic through global + remap plugins")
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.MakeCurlCommand(f'-s -D - -o /dev/null --proxy 127.0.0.1:{self.ts.Variables.port} "http://test.example/"', ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            '200 OK', 'request should match the remap rule and be served')
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

        # Drive a generator request: it serves a 4096-byte body through a PluginVC intercept.
        tr = Test.AddTestRun("Drive traffic through a PluginVC intercept plugin")
        tr.MakeCurlCommand(
            f'-s -o /dev/null --proxy 127.0.0.1:{self.ts.Variables.port} "http://gen.example/nocache/4096"', ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server

    def checkMetric(self, description, metric, message):
        tr = Test.AddTestRun(description)
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.Command = f'traffic_ctl metric get {metric}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(f'{re.escape(metric)} [1-9]', message)
        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server

    def checkMetrics(self):
        self.checkMetric(
            "Global plugin invocation counter is non-zero", 'proxy.process.plugin.header_rewrite.invocations',
            'global header_rewrite invocations should be counted')
        self.checkMetric(
            "Remap plugin invocation counter is non-zero", 'proxy.process.plugin.conf_remap.invocations',
            'remap conf_remap invocations should be counted')
        self.checkMetric(
            "PluginVC intercept bytes counter is non-zero", 'proxy.process.plugin.generator.bytes',
            'generator PluginVC transport bytes should be counted')
        self.checkMetric(
            "PluginVC intercept transfers counter is non-zero", 'proxy.process.plugin.generator.transfers',
            'generator PluginVC transfer events should be counted')

    def run(self):
        self.driveTraffic()
        self.checkMetrics()


TestPerPluginWorkloadCounters().run()
