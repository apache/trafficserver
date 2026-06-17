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
Verify that @ headers are stripped before global plugin and remap hooks run.
'''

replay_file = "replay/at_headers.replay.yaml"
plugin_name = "at_header_probe.so"
global_request_probe = "@Client-Test"
response_probe = "@Origin-Test"
request_added = "@Plugin-Request"
response_added = "@Plugin-Response"
remap_request_probe = "@Client-Remap-Test"

server = Test.MakeVerifierServerProcess("server", replay_file)
ts = Test.MakeATSProcess("ts", command="traffic_manager", enable_cache=False)

Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'at_headers', 'plugins', '.libs', plugin_name), ts,
    f"{global_request_probe} {response_probe} {request_added} {response_added}")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
})

ts.Disk.remap_config.AddLine(f"map /global http://127.0.0.1:{server.Variables.http_port}/global")
ts.Disk.remap_config.AddLine(
    f"map /remap http://127.0.0.1:{server.Variables.http_port}/remap "
    f"@plugin={plugin_name} @pparam={remap_request_probe}")

ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "stripped internal @ header from client request: @client-test", "Client-side @ header removals should be logged.")
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "stripped internal @ header from origin response: @origin-test", "Origin-side @ header removals should be logged.")
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "stripped internal @ header from client request: @client-remap-test",
    "Client-side @ header removals should be logged before remap plugins run.")
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    "saw unexpected request header", "Plugins should not see client-supplied @ headers.")
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    "saw unexpected response header", "Plugins should not see origin-supplied @ headers.")
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    "saw unexpected remap request header", "Remap plugins should not see client-supplied @ headers.")
ts.Disk.diags_log.Content += Testers.ExcludesExpression("FATAL:", "ATS should not log fatal errors while stripping @ headers.")
ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    "Unrecognized configuration value", "ATS should not log warnings about unrecognized configuration values.")

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client", replay_file, http_ports=[ts.Variables.port])
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
