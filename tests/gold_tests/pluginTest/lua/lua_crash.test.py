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
Cause Lua crath
'''

Test.SkipUnless(
    Condition.PluginExists('tslua.so'),
)

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=tslua.so @pparam={}/crash.lua'.format(Test.TestDirectory)
)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ts_lua',
    # The lower the number of max states, the greater the chance crash.sh will fail.
    'proxy.config.plugin.lua.max_states': 1
})

tr = Test.AddTestRun()
tr.Processes.Default.Command = "curl -v http://127.0.0.1:{0}".format(ts.Variables.port)
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server

tr = Test.AddTestRun()
dq = '"'
tr.Processes.Default.Command = (
    f"bash -c 'renice -n 19 -p $$(ps -e -ocmd,pid | grep ^traffic_server | sed {dq}s/^traffic_server//{dq})'"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = f"bash -c 'nice --adjustment=-10 {Test.TestDirectory}/crash.sh {ts.Variables.port}'"
tr.Processes.Default.ReturnCode = 0
