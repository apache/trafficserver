'''
Verify how ATS handles remap rules near and beyond the argument limit.
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
A remap rule with close to the maximum number of arguments must load and serve,
while a rule that exceeds the maximum must be rejected without ATS crashing.

This exercises remap_load_plugin's parameter copy buffers (sized to
BUILD_TABLE_MAX_ARGS) and the parser's argument count guard.
'''

# ----------------------------------------------------------------------------
# Case 1: close to the maximum number of chained-plugin parameters must load
# and serve. Using a count just under BUILD_TABLE_MAX_ARGS (2048) pins the copy
# buffers to that size; it would fail if they shrank below it. The total
# argument count (these plus the plugin/pparam keywords) must stay under 2048,
# and the repeated override is harmless: only the argument count matters.
# ----------------------------------------------------------------------------
ts_ok = Test.MakeATSProcess("ts-ok")
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: overflow.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

ts_ok.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
})

num_params = 2040
trailing_params = ' '.join(['@pparam=proxy.config.diags.debug.enabled=0'] * num_params)
ts_ok.Disk.remap_config.AddLine(
    'map http://overflow.example.com http://127.0.0.1:{0} '
    '@plugin=conf_remap.so @pparam=proxy.config.diags.debug.enabled=0 '
    '@plugin=conf_remap.so {1}'.format(server.Variables.Port, trailing_params))

ts_ok.Disk.traffic_out.Content = Testers.ExcludesExpression(
    "failed assertion", "traffic_server must not abort parsing remap.config")
ts_ok.Disk.traffic_out.Content += Testers.ExcludesExpression(
    "received signal", "traffic_server must not crash parsing remap.config")

tr = Test.AddTestRun("near-limit rule loads and serves")
tr.MakeCurlCommand(
    '--proxy 127.0.0.1:{0} "http://overflow.example.com" -H "Proxy-Connection: keep-alive" --verbose'.format(ts_ok.Variables.port),
    ts=ts_ok)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts_ok)
tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("200 OK", "expected a 200 response from origin")
tr.StillRunningAfter = ts_ok
tr.StillRunningAfter = server

# ----------------------------------------------------------------------------
# Case 2: exceeding BUILD_TABLE_MAX_ARGS arguments on a single line must be
# reported as an error and fail the load cleanly, never a crash from writing
# past the parser's argument arrays.
# ----------------------------------------------------------------------------
ts_bad = Test.MakeATSProcess("ts-bad")

num_args = 2100
args = ' '.join(['@pparam=proxy.config.diags.debug.enabled=0'] * num_args)
ts_bad.Disk.remap_config.AddLine('map http://overflow.example.com http://127.0.0.1:80 {0}'.format(args))

# A malformed line aborts the whole remap load, so ATS is expected to fail to
# start rather than crash while parsing the oversized line.
ts_bad.ReturnCode = 33
ts_bad.Ready = 0
ts_bad.Disk.diags_log.Content = Testers.ContainsExpression("too many arguments", "the oversized line must be rejected")
ts_bad.Disk.diags_log.Content += Testers.ExcludesExpression("received signal", "traffic_server must not crash parsing remap.config")
ts_bad.Disk.traffic_out.Content = Testers.ExcludesExpression(
    "failed assertion", "traffic_server must not abort parsing remap.config")

tr2 = Test.AddTestRun("oversized rule is rejected")

# Wait for the rejection to be logged. This cannot be the ATS readiness
# criteria because the process exits, which autest could observe before the log
# message is written.
watcher = Test.Processes.Process("watcher")
watcher.Command = "sleep 30"
watcher.Ready = When.FileContains(ts_bad.Disk.diags_log.Name, "too many arguments")
watcher.StartBefore(ts_bad)

tr2.Processes.Default.Command = "echo done"
tr2.TimeOut = 10
tr2.Processes.Default.StartBefore(watcher)
