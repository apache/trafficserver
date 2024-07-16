'''
Test global header_rewrite with set-redirect operator.
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
Test global header_rewrite with set-redirect operator.
'''

Test.ContinueOnFail = True
ts = Test.MakeATSProcess("ts", block_for_debug=False)
server = Test.MakeOriginServer("server")

# Configure the server to return 200 responses. The rewrite rules below set a
# non-200 status, so if curl gets a 200 response something went wrong.
Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: no_path.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.records_config.update(
    {
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.show_location': 0,
        'proxy.config.diags.debug.tags': 'header',
    })
ts.Setup.CopyAs('rules/glob_set_redirect.conf', Test.RunDirectory)

ts.Disk.plugin_config.AddLine(f'header_rewrite.so {Test.RunDirectory}/glob_set_redirect.conf')

# Run operator on Read Response Hdr hook (ID:REQUEST == 0).
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.Command = (f'curl --head 127.0.0.1:{ts.Variables.port} -H "Host: 127.0.0.1:{server.Variables.Port}" --verbose')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/set-redirect-glob.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
ts.Disk.traffic_out.Content = "gold/header_rewrite-tag.gold"

# Run operator on Send Response Hdr hook (ID:REQUEST == 1).
tr = Test.AddTestRun()
tr.Processes.Default.Command = (f'curl --head 127.0.0.1:{ts.Variables.port} -H "Host: 127.0.0.1:{server.Variables.Port}" --verbose')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/set-redirect-glob.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
ts.Disk.traffic_out.Content = "gold/header_rewrite-tag.gold"
