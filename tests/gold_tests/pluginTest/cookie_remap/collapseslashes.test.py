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

'''
Test.SkipUnless(Condition.PluginExists('cookie_remap.so'))
# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)
Test.ContinueOnFail = True
Test.testName = "cookie_remap: plugin collapses consecutive slashes"

# Define default ATS
ts = Test.MakeATSProcess("ts")

# We only run a server to capture ATS outbound requests
# and verify it collapsed the double //
server = Test.MakeOriginServer("server", ip='127.0.0.10')

request_header = {"headers": "GET /i/like/cheetos?.done=http://finance.yahoo.com HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)

# Setup the remap configuration
config_path = os.path.join(Test.TestDirectory, "configs/collapseconfig.txt")
with open(config_path, 'r') as config_file:
    config1 = config_file.read()

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cookie_remap.*|http.*|dns.*',
})

config1 = config1.replace("$PORT", str(server.Variables.Port))

ts.Disk.File(ts.Variables.CONFIGDIR +"/collapseconfig.txt", exists=False, id="config1")
ts.Disk.config1.WriteOn(config1)

ts.Disk.remap_config.AddLine(
    'map http://www.example.com/magic http://shouldnothit.com @plugin=cookie_remap.so @pparam=config/collapseconfig.txt'
)

tr = Test.AddTestRun("collapse consecutive forward slashes")
tr.Processes.Default.Command = '''
curl 
--proxy 127.0.0.1:{0} 
"http://www.example.com/magic" 
-H "Proxy-Connection: keep-alive" 
--verbose
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts

server.Streams.All = "gold/collapseslashes.gold"

