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
Test spawning a thread in a transaction hook continuation, and getting a result from it, without blocking event task.
'''

plugin_name = "polite_hook_wait"

server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "112233"}
server.addResponse("sessionlog.json", request_header, response_header)

# Disable the cache to make sure each request is forwarded to the origin
# server.
ts = Test.MakeATSProcess("ts", enable_cache=False, block_for_debug=False)

ts.Disk.records_config.update(
    {
        'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
        'proxy.config.url_remap.remap_required': 1,
        'proxy.config.diags.debug.enabled': 3,
        'proxy.config.diags.debug.tags': f'{plugin_name}',
    })

rp = os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', 'polite_hook_wait', '.libs', f'{plugin_name}.so')
ts.Setup.Copy(rp, ts.Env['PROXY_CONFIG_PLUGIN_PLUGIN_DIR'])

ts.Disk.plugin_config.AddLine(f"{plugin_name}.so")

ts.Disk.remap_config.AddLine("map http://myhost.test http://127.0.0.1:{0}".format(server.Variables.Port))

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.MakeCurlCommand('--verbose --ipv4 --header "Host:myhost.test" http://localhost:{}/ 2>curl.txt'.format(ts.Variables.port))
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.MakeCurlCommand('--verbose --ipv4 --header "Host:myhost.test" http://localhost:{}/ 2>curl.txt'.format(ts.Variables.port))
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
# Later versions of curl add a "using HTTP" line to the output, so we filter it
# out to keep this test compatible with old and new versions.
tr.Processes.Default.Command = "grep -F HTTP/ curl.txt | grep -v 'using HTTP'"
tr.Processes.Default.Streams.stdout = "curl.gold"
tr.Processes.Default.ReturnCode = 0
