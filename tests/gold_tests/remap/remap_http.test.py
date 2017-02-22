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
# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl","Curl need to be installed on system for this test to work")
    )
Test.ContinueOnFail=True
# Define default ATS
ts=Test.MakeATSProcess("ts")
server=Test.MakeOriginServer("server")

Test.testName = ""
request_header={"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
#expected response from the origin server
response_header={"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

#add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)
ts.Disk.records_config.update({
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'url.*',
    })

ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map http://www.example.com:8080 http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# call localhost straight
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl "http://127.0.0.1:{0}/" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode=0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr="gold/remap-hitATS-404.gold"
tr.StillRunningAfter=server

# www.example.com host
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl --proxy 127.0.0.1:{0} "http://www.example.com" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode=0
tr.Processes.Default.Streams.stderr="gold/remap-200.gold"

# www.example.com:80 host
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl  --proxy 127.0.0.1:{0} "http://www.example.com:80/" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode=0
tr.Processes.Default.Streams.stderr="gold/remap-200.gold"

# www.example.com:8080 host
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl  --proxy 127.0.0.1:{0} "http://www.example.com:8080" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode=0
tr.Processes.Default.Streams.stderr="gold/remap-200.gold"

# no rule for this
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl  --proxy 127.0.0.1:{0} "http://www.test.com/" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode=0
tr.Processes.Default.Streams.stderr="gold/remap-404.gold"

# bad port
tr=Test.AddTestRun()
tr.Processes.Default.Command='curl  --proxy 127.0.0.1:{0} "http://www.example.com:1234/" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode=0
tr.Processes.Default.Streams.stderr="gold/remap-404.gold"

     
