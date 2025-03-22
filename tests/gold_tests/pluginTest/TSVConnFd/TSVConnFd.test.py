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
Test TSVConnFdCreate() TS API call.
'''

plugin_name = "TSVConnFd"

server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "112233"}
server.addResponse("sessionlog.json", request_header, response_header)

# File to be deleted when tests are fully completed.
#
InProgressFilePathspec = os.path.join(Test.RunDirectory, "in_progress")

ts = Test.MakeATSProcess("ts", block_for_debug=False)

ts.Disk.records_config.update(
    {
        'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
        'proxy.config.url_remap.remap_required': 1,
        'proxy.config.diags.debug.enabled': 3,
        'proxy.config.diags.debug.tags': f'{plugin_name}',
    })

rp = os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', 'TSVConnFd', '.libs', f'{plugin_name}.so')
ts.Setup.Copy(rp, ts.Env['PROXY_CONFIG_PLUGIN_PLUGIN_DIR'])

Test.GetTcpPort("tcp_port")

ts.Disk.plugin_config.AddLine(f"{plugin_name}.so {InProgressFilePathspec} {ts.Variables.tcp_port}")

ts.Disk.remap_config.AddLine("map http://myhost.test http://127.0.0.1:{0}".format(server.Variables.Port))

# Dummy transaction to trigger plugin.
#
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.CurlCommandMulti(
    f'touch {InProgressFilePathspec} ; ' +
    f'{{curl}} --verbose --ipv4 --header "Host:myhost.test" http://localhost:{ts.Variables.port}/')
tr.Processes.Default.ReturnCode = 0

# Give tests up to 10 seconds to complete.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 15 1 -f ' + InProgressFilePathspec)
tr.Processes.Default.ReturnCode = 0
