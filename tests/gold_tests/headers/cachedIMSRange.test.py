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

Test.Summary = '''
Test revalidating cached objects
'''

testName = "RevalidateCacheObject"
Test.ContinueOnFail = True

# Set up Origin server
# request_header is from ATS to origin; response from Origin to ATS
# lookup_key is to make unique response in origin for header "UID" that will pass in ATS request
server = Test.MakeOriginServer("server", lookup_key="{%UID}")
# Initial request
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\nUID: Fill\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nLast-Modified: Tue, 08 May 2018 15:49:41 GMT\r\nCache-Control: max-age=1\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"}
server.addResponse("sessionlog.json", request_header, response_header)
# IMS revalidation request
request_IMS_header = {
    "headers": "GET / HTTP/1.1\r\nUID: IMS\r\nIf-Modified-Since: Tue, 08 May 2018 15:49:41 GMT\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""}
response_IMS_header = {"headers": "HTTP/1.1 304 Not Modified\r\nConnection: close\r\nCache-Control: max-age=1\r\n\r\n",
                       "timestamp": "1469733493.993", "body": None}
server.addResponse("sessionlog.json", request_IMS_header, response_IMS_header)

# EtagFill
request_etagfill_header = {"headers": "GET /etag HTTP/1.1\r\nHost: www.example.com\r\nUID: EtagFill\r\n\r\n",
                           "timestamp": "1469733493.993", "body": None}
response_etagfill_header = {
    "headers": "HTTP/1.1 200 OK\r\nETag: myetag\r\nConnection: close\r\nCache-Control: max-age=1\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"}
server.addResponse("sessionlog.json", request_etagfill_header, response_etagfill_header)
# INM revalidation
request_INM_header = {"headers": "GET /etag HTTP/1.1\r\nUID: INM\r\nIf-None-Match: myetag\r\nHost: www.example.com\r\n\r\n",
                      "timestamp": "1469733493.993", "body": None}
response_INM_header = {
    "headers": "HTTP/1.1 304 Not Modified\r\nConnection: close\r\nETag: myetag\r\nCache-Control: max-age=1\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": None}
server.addResponse("sessionlog.json", request_INM_header, response_INM_header)

# object changed to 0 byte
request_noBody_header = {"headers": "GET / HTTP/1.1\r\nUID: noBody\r\nHost: www.example.com\r\n\r\n",
                         "timestamp": "1469733493.993", "body": ""}
response_noBody_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\nCache-Control: max-age=3\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""}
server.addResponse("sessionlog.json", request_noBody_header, response_noBody_header)

# etag object now is a 404. Yeah, 404s don't usually have Cache-Control, but, ATS's default is to cache 404s for a while.
request_etagfill_header = {"headers": "GET /etag HTTP/1.1\r\nHost: www.example.com\r\nUID: EtagError\r\n\r\n",
                           "timestamp": "1469733493.993", "body": None}
response_etagfill_header = {
    "headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\nCache-Control: max-age=3\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""}
server.addResponse("sessionlog.json", request_etagfill_header, response_etagfill_header)

# ATS Configuration
ts = Test.MakeATSProcess("ts")
ts.Disk.plugin_config.AddLine('xdebug.so')
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.http.response_via_str': 3,
})

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# Test 0 - Fill a 3 byte object with Last-Modified time into cache.
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: Fill" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-miss.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 1 - Once it goes stale, fetch it again. We expect Origin to get IMS
# request, and serve a 304. We expect ATS to refresh the object, and give
# a 200 to user
tr = Test.AddTestRun()
tr.DelayStart = 2
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: IMS" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-hit-stale.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 2 - Once it goes stale, fetch it via a range request. We expect
# Origin to get IMS request, and serve a 304. We expect ATS to refresh the
# object, and give a 206 to user
tr = Test.AddTestRun()
tr.DelayStart = 2
tr.Processes.Default.Command = 'curl --range 0-1 -s -D - -v --ipv4 --http1.1 -H"UID: IMS" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-hit-stale-206.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 3 - Fill a new object with an Etag. Not checking the output here.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: EtagFill" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/etag'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 4 - Once the etag object goes stale, fetch it again. We expect
# Origin to get INM request, and serve a 304. We expect ATS to refresh the
# object, and give a 200 to user
tr = Test.AddTestRun()
tr.DelayStart = 2
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: INM" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/etag'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-hit-stale-INM.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 5 - Once the etag object goes stale, fetch it via a range request.
# We expect Origin to get INM request, and serve a 304. We expect ATS to
# refresh the object, and give a 206 to user
tr = Test.AddTestRun()
tr.DelayStart = 2
tr.Processes.Default.Command = 'curl --range 0-1 -s -D - -v --ipv4 --http1.1 -H"UID: INM" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/etag'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_body-hit-stale-206-etag.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 6 - The origin changes the initial LMT object to 0 byte. We expect ATS to fetch and serve the new 0 byte object.
tr = Test.AddTestRun()
tr.DelayStart = 3
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: noBody" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_nobody-hit-stale.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 7 - Fetch the new 0 byte object again when fresh in cache to ensure its still a 0 byte object.
tr = Test.AddTestRun()
tr.DelayStart = 3
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: noBody" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_nobody-hit-stale.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 8 - The origin changes the etag object to 0 byte 404. We expect ATS to fetch and serve the 404 0 byte object.
tr = Test.AddTestRun()
tr.DelayStart = 2
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: EtagError" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/etag'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_error_nobody.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 9 - Fetch the 0 byte etag object again when fresh in cache to ensure its still a 0 byte object
tr = Test.AddTestRun()
tr.DelayStart = 2
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: EtagError" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/etag'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_error_nobody.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
