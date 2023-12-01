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
Test adding hooks
'''

Test.ContinueOnFail = True

server = Test.MakeOriginServer("server")

request_header = {"headers": "GET /argh HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=False, enable_cache=False)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.tags': 'test',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.url_remap.remap_required': 0,
    })

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'hook_add_plugin.so'), ts)

ts.Disk.remap_config.AddLine("map http://one http://127.0.0.1:{0}".format(server.Variables.Port))

tr = Test.AddTestRun()
# Probe server port to check if ready.
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
#
tr.Processes.Default.Command = ('curl --verbose --ipv4 --header "Host: one" http://localhost:{0}/argh'.format(ts.Variables.port))
tr.Processes.Default.ReturnCode = 0

# Look at the debug output from the plugin
ts.Disk.traffic_out.Content = "hook_add.gold"
