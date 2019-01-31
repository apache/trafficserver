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
Test.testName = "cookie_remap: cookie regex match and substition"

# Define default ATS
ts = Test.MakeATSProcess("ts")

# First server is run during first test and
# second server is run during second test
server = Test.MakeOriginServer("server", ip='127.0.0.10')

request_header = {"headers": "GET /regexmatches?cookies=oreos-chipsahoy-icecream HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)

server2 = Test.MakeOriginServer("server2", ip='127.0.0.11')
request_header2 = {"headers": "GET /regexdoesntmatch HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header2 = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server2.addResponse("sessionfile.log", request_header2, response_header2)

# Setup the remap configuration
config_path = os.path.join(Test.TestDirectory, "configs/regexconfig.txt")
with open(config_path, 'r') as config_file:
    config1 = config_file.read()

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cookie_remap.*|http.*|dns.*',
})

config1 = config1.replace("$PORT", str(server.Variables.Port))
config1 = config1.replace("$ALTPORT", str(server2.Variables.Port))

ts.Disk.File(ts.Variables.CONFIGDIR +"/regexconfig.txt", exists=False, id="config1")
ts.Disk.config1.WriteOn(config1)

ts.Disk.remap_config.AddLine(
    'map http://www.example.com/magic http://shouldnothit.com @plugin=cookie_remap.so @pparam=config/regexconfig.txt'
)

# Positive test case that remaps because cookie regex matches
tr = Test.AddTestRun("cookie regex matches")
tr.Processes.Default.Command = '''
curl 
--proxy 127.0.0.1:{0} 
"http://www.example.com/magic" 
-H"Cookie: fpbeta=ilove-oreos-chipsahoy-icecream"
-H "Proxy-Connection: keep-alive" 
--verbose
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts

server.Streams.All = "gold/regexmatches.gold"

# Negative test case that doesn't remap because cookie regex doesn't match
tr = Test.AddTestRun("cookie regex doesn't match")
tr.Processes.Default.Command = '''
curl 
--proxy 127.0.0.1:{0} 
"http://www.example.com/magic" 
-H"Cookie: fpbeta=etc" 
-H "Proxy-Connection: keep-alive" 
--verbose
'''.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server2, ready=When.PortOpen(server2.Variables.Port))
tr.StillRunningAfter = ts

server2.Streams.All = "gold/regexdoesntmatch.gold"
