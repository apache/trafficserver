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
# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)
Test.ContinueOnFail = True
Test.testName = "cookie_remap: Tests when matrix parameters are present"

# Define default ATS
ts = Test.MakeATSProcess("ts")

# We just need a server to capture ATS outgoing requests
# so that we can verify the remap rules
# That's why I am not adding any canned request/response
server = Test.MakeOriginServer("server", ip='127.0.0.10')

# Setup the remap configuration
config_path = os.path.join(Test.TestDirectory, "configs/matrixconfig.txt")
with open(config_path, 'r') as config_file:
    config1 = config_file.read()

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cookie_remap.*|http.*|dns.*',
})

config1 = config1.replace("$PORT", str(server.Variables.Port))

ts.Disk.File(ts.Variables.CONFIGDIR +"/matrixconfig.txt", exists=False, id="config1")
ts.Disk.config1.WriteOn(config1)

ts.Disk.remap_config.AddLine(
    'map http://www.example.com/eighth http://shouldnothit.com @plugin=cookie_remap.so @pparam=config/matrixconfig.txt'
)
ts.Disk.remap_config.AddLine(
    'map http://www.example.com/ninth http://shouldnothit.com @plugin=cookie_remap.so @pparam=config/matrixconfig.txt'
)
ts.Disk.remap_config.AddLine(
    'map http://www.example.com/tenth http://shouldnothit.com @plugin=cookie_remap.so @pparam=config/matrixconfig.txt'
)
ts.Disk.remap_config.AddLine(
    'map http://www.example.com/eleventh http://shouldnothit.com @plugin=cookie_remap.so @pparam=config/matrixconfig.txt'
)

tr = Test.AddTestRun("path is substituted")
tr.Processes.Default.Command = '''
curl \
--proxy 127.0.0.1:{0} \
"http://www.example.com/eighth/magic;matrix=1" \
-H "Proxy-Connection: keep-alive" \
--verbose \
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("path is substituted when matrix \
                      and query is present")
tr.Processes.Default.Command = '''
curl \
--proxy 127.0.0.1:{0} \
"http://www.example.com/eighth/magic;matrix=1?hello=10" \
-H "Proxy-Connection: keep-alive" \
--verbose \
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("Another $path substitution passing matrix \
                       and query and replacing query")
tr.Processes.Default.Command = '''
curl \
--proxy 127.0.0.1:{0} \
"http://www.example.com/ninth/magic;matrix=5?hello=16" \
-H "Proxy-Connection: keep-alive" \
--verbose \
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("$path substitution in sendto and \
                      inserting matrix parameters in remap")
tr.Processes.Default.Command = '''
curl \
--proxy 127.0.0.1:{0} \
"http://www.example.com/tenth/magic" \
-H "Proxy-Connection: keep-alive" \
--verbose \
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("inserting matrix params in remap and \
                      passing along query string")
tr.Processes.Default.Command = '''
curl \
--proxy 127.0.0.1:{0} \
"http://www.example.com/tenth/magic?query=10" \
-H "Proxy-Connection: keep-alive" \
--verbose \
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

tr = Test.AddTestRun("Another test to verify matrix and query \
                      params are passed along")
tr.Processes.Default.Command = '''
curl \
--proxy 127.0.0.1:{0} \
"http://www.example.com/eleventh/magic;matrix=4?query=12" \
-H "Proxy-Connection: keep-alive" \
-H "Proxy-Connection: keep-alive" \
--verbose \
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

server.Streams.All = "gold/matrix.gold"

