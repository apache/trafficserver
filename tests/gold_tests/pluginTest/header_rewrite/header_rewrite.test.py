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

import os

Test.Summary = '''
Test a basic remap of a http connection
'''

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

request_header = {"headers": "GET /503 HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.show_location': 0,
        'proxy.config.diags.debug.tags': 'header|http',
    })
# The following rule changes the status code returned from origin server to 303
ts.Setup.CopyAs('rules/rule.conf', Test.RunDirectory)
ts.Disk.plugin_config.AddLine('header_rewrite.so {0}/rule.conf'.format(Test.RunDirectory))
ts.Disk.remap_config.AddLine('map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port))
ts.Disk.remap_config.AddLine('map http://www.example.com:8080 http://127.0.0.1:{0}'.format(server.Variables.Port))

# Add logging configuration to test the new plugin tag field
ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: plugin_status
      format: '%<cqhm> %<cqup> %<pssc> %<prscs>'
  logs:
    - filename: plugin-status-test
      format: plugin_status
'''.split("\n"))

plugin_status_log = os.path.join(ts.Variables.LOGDIR, 'plugin-status-test.log')
ts.Disk.File(plugin_status_log, exists=True, content='gold/plugin-status-test.gold')

# call localhost straight
tr = Test.AddTestRun("Header Rewrite 200 to 303")
tr.MakeCurlCommand(
    '--proxy 127.0.0.1:{0} "http://www.example.com" -H "Proxy-Connection: keep-alive" --verbose'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = "gold/header_rewrite-303.gold"
tr.StillRunningAfter = server

tr = Test.AddTestRun("Header Rewrite 503 to 502")
tr.MakeCurlCommand(
    '--proxy 127.0.0.1:{0} "http://www.example.com/503" -H "Proxy-Connection: keep-alive" --verbose'.format(ts.Variables.port),
    ts=ts)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/header_rewrite-502.gold"
tr.StillRunningAfter = server

ts.Disk.traffic_out.Content = "gold/header_rewrite-tag.gold"

# Verify the plugin status log file contains the expected plugin tag

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
tr = Test.AddTestRun()
tr.Processes.Default.Command = (os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + f' 60 1 -f {plugin_status_log}')
tr.Processes.Default.ReturnCode = 0
