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

pluginName = 'ats_ssl_plugin'
path = os.path.abspath(".")
configFile = './packages/rhel.6.5.package/conf/trafficserver/ats_ssl_session_reuse.xml'

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': f'{pluginName}',
})

ts.Disk.plugin_config.AddLine(f'# {path}/{pluginName}.so {configFile}')
ts.Disk.remap_config.AddLine(f'map http://www.example.com http://127.0.0.1:{server.Variables.Port}')

goldFile = os.path.join(Test.RunDirectory, f"{pluginName}.gold")
with open(goldFile, 'w+') as jf:
    jf.write(f"``loading plugin ``{pluginName}.so``")

# call localhost straight
tr = Test.AddTestRun()
tr.Processes.Default.Command = f'curl --proxy 127.0.0.1:{ts.Variables.port} "http://www.example.com" --verbose'
tr.Processes.Default.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = server

# ts.Streams.All=goldFile
# ts.Disk.diags_log.Content+=goldFile
