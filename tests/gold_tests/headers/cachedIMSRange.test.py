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
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\nUID: Fill\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers":
        "HTTP/1.1 200 OK\r\nConnection: close\r\nLast-Modified: Tue, 08 May 2018 15:49:41 GMT\r\nCache-Control: max-age=1\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"
}
server.addResponse("sessionlog.json", request_header, response_header)
# IMS revalidation request
request_IMS_header = {
    "headers": "GET / HTTP/1.1\r\nUID: IMS\r\nIf-Modified-Since: Tue, 08 May 2018 15:49:41 GMT\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_IMS_header = {
    "headers": "HTTP/1.1 304 Not Modified\r\nConnection: close\r\nCache-Control: max-age=1\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": None
}
server.addResponse("sessionlog.json", request_IMS_header, response_IMS_header)

# EtagFill
request_etagfill_header = {
    "headers": "GET /etag HTTP/1.1\r\nHost: www.example.com\r\nUID: EtagFill\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": None
}
response_etagfill_header = {
    "headers": "HTTP/1.1 200 OK\r\nETag: myetag\r\nConnection: close\r\nCache-Control: max-age=1\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx"
}
server.addResponse("sessionlog.json", request_etagfill_header, response_etagfill_header)
# INM revalidation
request_INM_header = {
    "headers": "GET /etag HTTP/1.1\r\nUID: INM\r\nIf-None-Match: myetag\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": None
}
response_INM_header = {
    "headers": "HTTP/1.1 304 Not Modified\r\nConnection: close\r\nETag: myetag\r\nCache-Control: max-age=1\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": None
}
server.addResponse("sessionlog.json", request_INM_header, response_INM_header)

# object changed to 0 byte
request_noBody_header = {
    "headers": "GET / HTTP/1.1\r\nUID: noBody\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_noBody_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\nCache-Control: max-age=3\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
server.addResponse("sessionlog.json", request_noBody_header, response_noBody_header)

# etag object now is a 404. Yeah, 404s don't usually have Cache-Control, but, ATS's default is to cache 404s for a while.
request_etagfill_header = {
    "headers": "GET /etag HTTP/1.1\r\nHost: www.example.com\r\nUID: EtagError\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": None
}
response_etagfill_header = {
    "headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\nCache-Control: max-age=3\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
server.addResponse("sessionlog.json", request_etagfill_header, response_etagfill_header)

# ATS Configuration
ts = Test.MakeATSProcess("ts", enable_tls=True)
ts.Disk.plugin_config.AddLine('xdebug.so')
ts.addDefaultSSLFiles()
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.http.response_via_str': 3,
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    })

default_304_host = 'www.default304.test'
regex_remap_conf_file = "maps.reg"
ts.Disk.remap_config.AddLines(
    [
        f'map https://{default_304_host}/ http://127.0.0.1:{server.Variables.Port}/ '
        f'@plugin=regex_remap.so @pparam={regex_remap_conf_file} @pparam=no-query-string @pparam=host',
        f'map http://{default_304_host}/ http://127.0.0.1:{server.Variables.Port}/ '
        f'@plugin=regex_remap.so @pparam={regex_remap_conf_file} @pparam=no-query-string @pparam=host',
        f'map / http://127.0.0.1:{server.Variables.Port}',
    ])

ts.Disk.MakeConfigFile(regex_remap_conf_file).AddLine(f'//.*/ http://127.0.0.1:{server.Variables.Port} @status=304')

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

# Test 3 - Test 304 response served from a regex-remap rule with HTTP.
tr = Test.AddTestRun()
tr.Processes.Default.Command = f'curl -vs http://127.0.0.1:{ts.Variables.port}/ -H "Host: {default_304_host}"'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.GoldFile("gold/http1_304.gold", case_insensitive=True)
tr.StillRunningAfter = server

# Test 4 - Test 304 response served from a regex-remap rule with HTTPS.
tr = Test.AddTestRun()
tr.Processes.Default.Command = f'curl -vs -k https://127.0.0.1:{ts.Variables.ssl_port}/ -H "Host: {default_304_host}"'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.GoldFile("gold/http1_304.gold", case_insensitive=True)
tr.StillRunningAfter = server

# Test 5 - Test 304 response served from a regex-remap rule with HTTP/2.
tr = Test.AddTestRun()
tr.Processes.Default.Command = f'curl -vs -k --http2 https://127.0.0.1:{ts.Variables.ssl_port}/ -H "Host: {default_304_host}"'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.GoldFile("gold/http2_304.gold", case_insensitive=True)
tr.StillRunningAfter = server

# Test 6 - Fill a new object with an Etag. Not checking the output here.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: EtagFill" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/etag'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 7 - Once the etag object goes stale, fetch it again. We expect
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

# Test 8 - Once the etag object goes stale, fetch it via a range request.
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

# Test 9 - The origin changes the initial LMT object to 0 byte. We expect ATS to fetch and serve the new 0 byte object.
tr = Test.AddTestRun()
tr.DelayStart = 3
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: noBody" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_nobody-hit-stale.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 10 - Fetch the new 0 byte object again when fresh in cache to ensure its still a 0 byte object.
tr = Test.AddTestRun()
tr.DelayStart = 3
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: noBody" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_req_nobody-hit-stale.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 11 - The origin changes the etag object to 0 byte 404. We expect ATS to fetch and serve the 404 0 byte object.
tr = Test.AddTestRun()
tr.DelayStart = 2
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: EtagError" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/etag'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_error_nobody.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Test 12 - Fetch the 0 byte etag object again when fresh in cache to ensure its still a 0 byte object
tr = Test.AddTestRun()
tr.DelayStart = 2
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H"UID: EtagError" -H "x-debug: x-cache,x-cache-key,via" -H "Host: www.example.com" http://localhost:{0}/etag'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "cache_and_error_nobody.gold"
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
