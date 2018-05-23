'''
Test cached responses and requests with bodies
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
Test cached responses and requests with bodies
'''

# Needs Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "curl needs to be installed on system for this test to work"),
    Condition.HasProgram("nc", "nc needs to be installed on system for this test to work")
)
Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

#**testname is required**
testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nLast-Modified: Tue, 08 May 2018 15:49:41 GMT\r\nCache-Control: max-age=1\r\n\r\n", "timestamp": "1469733493.993", "body": "xxx"}
server.addResponse("sessionlog.json", request_header, response_header)

# ATS Configuration
ts.Disk.plugin_config.AddLine('xdebug.so')
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.http.response_via_str': 3,
    'proxy.config.http.cache.http': 1,
    'proxy.config.http.wait_for_cache': 1,
})

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# Test 1 - 200 response and cache fill
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=1)
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{port}/'.format(port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-miss.gold"
tr.StillRunningAfter = ts

# Test 2 - 200 cached response and using netcat
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET / HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-hit.gold"
tr.StillRunningAfter = ts

# Test 3 - 200 cached response and trying to hide a request in the body
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET / HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''Content-Length: 71\r\n''\r\n''GET /index.html?evil=zorg810 HTTP/1.1\r\n''Host: dummy-host.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-hit.gold"
tr.StillRunningAfter = ts

# Test 4 - 200 cached response and Content-Length larger than bytes sent, MUST close
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET / HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: dummy-host.example.com\r\n''Cache-control: max-age=300\r\n''Content-Length: 100\r\n''\r\n''GET /index.html?evil=zorg810 HTTP/1.1\r\n''Host: dummy-host.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-hit_close.gold"
tr.StillRunningAfter = ts
