'''
Test __shutdown__ lua global plugin hook.
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
Test __shutdown__ lua global plugin hook
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")
ts = Test.MakeATSProcess("ts")

Test.Setup.Copy("global_shutdown.lua")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{}/'.format(server.Variables.Port))

# Use 2 states so the shutdown handler is called a predictable number of times.
ts.Disk.plugin_config.AddLine('tslua.so --states=2 {}/global_shutdown.lua'.format(Test.RunDirectory))

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ts_lua',
})

curl_and_args = '-s -D /dev/stdout -o /dev/stderr -x localhost:{} '.format(ts.Variables.port)

# 0 Test - Send a request to confirm the global plugin is active.
tr = Test.AddTestRun("Lua global read request hook fires for HTTP requests")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(curl_and_args + 'http://www.example.com/', ts=ts)
ps.ReturnCode = 0
tr.StillRunningAfter = ts

# Verify do_global_read_request was invoked for the HTTP request above.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    r'do_global_read_request called', 'do_global_read_request should be called for HTTP requests')

# After all test runs complete AuTest stops ATS, which fires TS_LIFECYCLE_SHUTDOWN_HOOK.
# The shutdown handler calls __shutdown__ once per Lua state (2 states configured).
ts.Disk.traffic_out.Content += Testers.ContainsExpression(r'__shutdown__ called', '__shutdown__ should be called on ATS shutdown')
