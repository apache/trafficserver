'''
Test plugin.yaml loading with inline config and enabled/disabled plugins.
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
Test that plugin.yaml is loaded instead of plugin.config, that inline config
works with header_rewrite.so, and that enabled: false skips a plugin.
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
        'proxy.config.diags.debug.tags': 'header_rewrite|plugin',
    })

ts.Disk.remap_config.AddLine("map http://example.com http://127.0.0.1:{0}".format(server.Variables.Port))

# Write plugin.yaml into the config directory. ATS will prefer this over
# plugin.config.  Uses inline config (scalar literal) with header_rewrite.so
# to set a custom response header.  xdebug.so is listed but disabled.
ts.Disk.MakeConfigFile("plugin.yaml").update(
    {
        "plugins":
            [
                {
                    "path": "header_rewrite.so",
                    "config": "cond %{SEND_RESPONSE_HDR_HOOK}\n  set-header X-Plugin-YAML \"loaded-from-inline\"\n",
                },
                {
                    "path": "xdebug.so",
                    "enabled": False,
                    "params": ["--enable=x-cache"],
                },
            ]
    })

# Test 1: Verify header_rewrite loaded via plugin.yaml sets the response header.
tr = Test.AddTestRun("Verify inline config sets response header")
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.MakeCurlCommand('-s -D- -o /dev/null -H "Host: example.com" http://127.0.0.1:{0}/test'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    "X-Plugin-YAML: loaded-from-inline", "Response should contain X-Plugin-YAML header set by inline config")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Test 2: Verify xdebug is NOT loaded (enabled: false). Send the X-Debug
# header and confirm the X-Cache header is absent from the response.
tr = Test.AddTestRun("Verify disabled plugin is not loaded")
tr.MakeCurlCommand(
    '-s -D- -o /dev/null -H "Host: example.com" -H "X-Debug: x-cache" http://127.0.0.1:{0}/test'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(
    "X-Cache:", "Response should NOT contain X-Cache header since xdebug is disabled")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Test 3: Verify the diags.log shows plugin.yaml was used.
tr = Test.AddTestRun("Verify plugin.yaml loading logged")
tr.Processes.Default.Command = "echo check diags.log"
tr.Processes.Default.ReturnCode = 0
ts.Disk.diags_log.Content += Testers.ContainsExpression("plugin.yaml loading", "diags.log should indicate plugin.yaml was loaded")
ts.Disk.diags_log.Content += Testers.ContainsExpression("skipped", "diags.log should indicate a plugin was skipped")
