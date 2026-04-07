'''
Test plugin.yaml loading with enabled/disabled plugins.
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
Test that plugin.yaml is loaded instead of plugin.config, and that
enabled: false skips a plugin.
'''

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")

request_header = {"headers": "GET /test HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts = Test.MakeATSProcess("ts", enable_cache=False)

ts.Disk.records_config.update(
    {
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'xdebug|plugin',
    })

ts.Disk.remap_config.AddLine("map http://example.com http://127.0.0.1:{0}".format(server.Variables.Port))

# Write plugin.yaml into the config directory. ATS will prefer this over
# plugin.config.  xdebug.so is enabled with params; header_rewrite.so is
# disabled.
ts.Disk.MakeConfigFile("plugin.yaml").update(
    {
        "plugins":
            [
                {
                    "path": "xdebug.so",
                    "params": ["--enable=x-cache"],
                },
                {
                    "path": "header_rewrite.so",
                    "enabled": False,
                },
            ]
    })

# Test 1: Verify xdebug loaded via plugin.yaml works — the X-Cache
# response header should appear when requested.
tr = Test.AddTestRun("Verify xdebug loaded from plugin.yaml")
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.MakeCurlCommand(
    '-s -D- -o /dev/null -H "Host: example.com" -H "X-Debug: x-cache" http://127.0.0.1:{0}/test'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "X-Cache:", "Response should contain X-Cache header from xdebug loaded via plugin.yaml")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Test 2: Verify the diags.log shows plugin.yaml was used and a plugin was skipped.
tr = Test.AddTestRun("Verify plugin.yaml loading logged")
tr.Processes.Default.Command = "echo check diags.log"
tr.Processes.Default.ReturnCode = 0
ts.Disk.diags_log.Content += Testers.ContainsExpression("plugin.yaml loading", "diags.log should indicate plugin.yaml was loaded")
ts.Disk.diags_log.Content += Testers.ContainsExpression("skipped", "diags.log should indicate a plugin was skipped")
