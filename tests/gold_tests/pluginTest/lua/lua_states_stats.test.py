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

Test.Summary = '''
Test lua states and stats functionality
'''

Test.SkipUnless(Condition.PluginExists('tslua.so'),)

Test.ContinueOnFail = True
# Define default ATS
server = Test.MakeOriginServer("server")

ts = Test.MakeATSProcess("ts")

Test.testName = "Lua states and stats"

Test.Setup.Copy("hello.lua")
Test.Setup.Copy("global.lua")
Test.Setup.Copy("metrics.sh")
Test.Setup.Copy("lifecycle_stats.sh")

# test to ensure origin server works
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.remap_config.AddLines(
    {
        'map / http://127.0.0.1:{}/'.format(server.Variables.Port),
        'map http://hello http://127.0.0.1:{}/'.format(server.Variables.Port) +
        ' @plugin=tslua.so @pparam={}/hello.lua'.format(Test.RunDirectory)
    })

ts.Disk.plugin_config.AddLine('tslua.so {}/global.lua'.format(Test.RunDirectory))

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ts_lua',
        'proxy.config.plugin.lua.max_states': 4,
    })

curl_and_args = '-s -D /dev/stdout -o /dev/stderr -x localhost:{} '.format(ts.Variables.port)

# 0 Test - Check for configured lua states
tr = Test.AddTestRun("Lua states")
ps = tr.Processes.Default  # alias
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = "traffic_ctl config match lua"
ps.Env = ts.Env
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("proxy.config.plugin.lua.max_states: 4", "expected 4 states")
tr.StillRunningAfter = ts

# 1 Test - Exercise lua script
tr = Test.AddTestRun("Lua hello")
ps = tr.Processes.Default  # alias
tr.MakeCurlCommand(curl_and_args + ' http://hello/hello', ts=ts)
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression("Hello, World", "hello world content")
tr.StillRunningAfter = ts

# 2 Test - Check for metrics
tr = Test.AddTestRun("Check for metrics")
ps = tr.Processes.Default  # alias
ps.Command = "bash -c ./metrics.sh"
ps.Env = ts.Env
ps.ReturnCode = 0
tr.StillRunningAfter = ts

# 3 Test - Check for developer lifecycle stats
tr = Test.AddTestRun("Check for lifecycle stats")
ps = tr.Processes.Default  # alias
ps.Command = "bash -c ./lifecycle_stats.sh"
ps.Env = ts.Env
ps.ReturnCode = 0
tr.StillRunningAfter = ts
