'''
Test how we handle image/webp
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
Checking that we don't serve image/webp to clients that do not support it
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

testName = "accept_webp"
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\nAccept: image/webp,image/png,image/svg+xml,image/*;q=0.8,video/*;q=0.8,*/*;q=0.5\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: image/webp\r\nCache-Control: max-age=300\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"}
server.addResponse("sessionlog.json", request_header, response_header)

# ATS Configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http_match',
    'proxy.config.http.cache.ignore_accept_mismatch': 0,
    'proxy.config.http.insert_response_via_str': 3,
})

ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# Test 1 - Request with image/webp support from the origin
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H "Accept: image/webp,image/png,image/svg+xml,image/*;q=0.8,video/*;q=0.8,*/*;q=0.5" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/accept_webp.gold"
tr.StillRunningAfter = ts

# Test 2 - Request with image/webp support from cache
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H "Accept: image/webp,image/png,image/svg+xml,image/*;q=0.8,video/*;q=0.8,*/*;q=0.5" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/accept_webp_cache.gold"
tr.StillRunningAfter = ts

# Test 3 - Request without image/webp support going to the origin - NOTE: the origin can't change the content-type :(
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H "Accept: image/png,image/svg+xml,image/*;q=0.8,video/*;q=0.8,*/*;q=0.5" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/accept_webp_jpeg.gold"
tr.StillRunningAfter = ts
