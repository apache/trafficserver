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
Test remap_stats plugin
'''
# Skip if plugins not present.
Test.SkipUnless(Condition.PluginExists('remap_stats.so'))

server = Test.MakeOriginServer("server")

Test.Setup.Copy("metrics_post.sh")

request_header = {"headers": "GET /argh HTTP/1.1\r\nHost: one\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=True)

ts.Disk.plugin_config.AddLine('remap_stats.so --post-remap-host')

ts.Disk.remap_config.AddLine("map http://one http://127.0.0.1:{0}".format(server.Variables.Port))
ts.Disk.remap_config.AddLine("map http://two http://127.0.0.1:{0}".format(server.Variables.Port))

ts.Disk.records_config.update(
    {
        'proxy.config.http.transaction_active_timeout_out': 2,
        'proxy.config.http.transaction_no_activity_timeout_out': 2,
        'proxy.config.http.connect_attempts_timeout': 2,
    })

# 0 Test - Curl host One
tr = Test.AddTestRun("curl host one")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'curl -o /dev/null -H "Host: one"' + ' http://127.0.0.1:{}/argh'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 1 Test - Curl host Two
tr = Test.AddTestRun("curl host two")
tr.Processes.Default.Command = 'curl -o /dev/null -H "Host: two"' + ' http://127.0.0.1:{}/badpath'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 2 Test - Gather output
tr = Test.AddTestRun("analyze stats")
tr.Processes.Default.Command = 'bash -c ./metrics_post.sh'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
