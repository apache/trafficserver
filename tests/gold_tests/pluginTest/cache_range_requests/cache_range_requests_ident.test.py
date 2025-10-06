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

import datetime
import os
import time

Test.Summary = '''
cache_range_requests X-Crr-Ident plugin test
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Ensure asset is stale and request with properly formed header.

Test.SkipUnless(
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = False
Test.testName = "cache_range_requests_ident"

# Define and configure origin server
server = Test.MakeOriginServer("server")

# default root
req_chk = {
    "headers": "GET / HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "uuid: none\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

res_chk = {"headers": "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "\r\n", "timestamp": "1469733493.993", "body": ""}

server.addResponse("sessionlog.json", req_chk, res_chk)

body = "lets go surfin now"
bodylen = len(body)

# baseline for testing
last_modified = "Fri, 07 Mar 2025 18:06:58 GMT"
etag = '"772102f4-56f4bc1e6d417"'

req_both = {
    "headers": "GET /both HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

res_both = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=1\r\n" +
        "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) + "Connection: close\r\n" + 'Etag: ' + etag + '\r\n' +
        'Last-Modified: ' + last_modified + '\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_both, res_both)

req_etag = {
    "headers": "GET /etag HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

res_etag = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=1\r\n" +
        "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) + "Connection: close\r\n" + 'Etag: ' + etag + '\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_etag, res_etag)

req_lm = {
    "headers": "GET /lm HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

res_lm = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=1\r\n" +
        "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) + "Connection: close\r\n" + 'Last-Modified: ' + last_modified +
        '\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_lm, res_lm)

# test for custom Ident header
req_custom = {
    "headers": "GET /custom HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

etag_custom = 'foo'

res_custom = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=1\r\n" +
        "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) + "Connection: close\r\n" + 'Etag: ' + etag_custom + '\r\n' + '\r\n',
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_custom, res_custom)

# Long lived asset for FRESH to STALE testing
req_fresh = {
    "headers": "GET /fresh HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

etag_fresh = 'fresh'

res_fresh = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=3600\r\n" +
        "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) + "Connection: close\r\n" + 'Etag: ' + etag_fresh + '\r\n' + '\r\n' +
        'Last-Modified: ' + last_modified + '\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_fresh, res_fresh)

# Define and configure ATS
ts = Test.MakeATSProcess("ts")

# cache range requests plugin remap
ts.Disk.remap_config.AddLines(
    [
        f'map http://ident http://127.0.0.1:{server.Variables.Port}' + ' @plugin=cache_range_requests.so @pparam=--consider-ident',
        f'map http://identheader http://127.0.0.1:{server.Variables.Port}' +
        ' @plugin=cache_range_requests.so @pparam=--consider-ident' + ' @pparam=--ident-header=CrrIdent',
    ])

# cache debug
ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache')

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cache_range_requests',
})

curl_and_args = '-s -D /dev/stdout -o /dev/stderr -x localhost:{} -H "x-debug: x-cache"'.format(ts.Variables.port)

##
## Stale to Fresh testing
##

## Fetch short lived assets into cache

# 0 Test - Fetch both asset into cache
tr = Test.AddTestRun("0- range cache load")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(curl_and_args + ' http://ident/both -r 0-', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

# 1 Test - Fetch etag asset into cache
tr = Test.AddTestRun("0- etag cache load")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://ident/etag -r 0-', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

# 2 Test - Fetch lm asset into cache
tr = Test.AddTestRun("0- lm cache load")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://ident/lm -r 0-', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

## both tests

# 3 Test - Ensure Etag header match results in hit-fresh
tr = Test.AddTestRun("0- both Etag check")
tr.DelayStart = 2  # Time to ensure stale
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://ident/both -r 0- -H 'X-Crr-Ident: Etag {etag}'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# 4 Test - Plugin expects Etag even if Last-Modified matches - hit-stale
tr = Test.AddTestRun("0- both Last-Modified check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f' http://ident/both -r 0- -H "X-Crr-Ident: Last-Modified {last_modified}"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

# 5 Test - Bad etag stays stale
tr = Test.AddTestRun("0- both bad Etag check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://ident/both -r 0- -H 'X-Crr-Ident: Etag no_match'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

## etag only supplied

# 6 Test - Ensure Etag header match results in hit-fresh
tr = Test.AddTestRun("0- etag Etag check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://ident/etag -r 0- -H 'X-Crr-Ident: Etag {etag}'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# 7 Test - Last modified will result in stale
tr = Test.AddTestRun("0- etag lm cache stale check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f' http://ident/etag -r 0- -H "X-Crr-Ident: Last-Modified {last_modified}"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache stale")
tr.StillRunningAfter = ts

# 8 Test - Bad etag stays stale
tr = Test.AddTestRun("0- etag bad Etag check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://ident/etag -r 0- -H 'X-Crr-Ident: Etag no_match'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

## last modified

# 9 Test - Last modified will result in fresh
tr = Test.AddTestRun("0- lm lm fresh check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f' http://ident/lm -r 0- -H "X-Crr-Ident: Last-Modified {last_modified}"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache fresh")
tr.StillRunningAfter = ts

# 10 Test - Ensure Etag header match results in hit-fresh
tr = Test.AddTestRun("0- lm Etag check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://ident/lm -r 0- -H 'X-Crr-Ident: Etag {etag}'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

## Fresh to stale testing

# 11 Test - Fetch "fresh" into cache
tr = Test.AddTestRun("0- fresh range cache load")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://ident/fresh -r 0-', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

# 12 Test - Ensure "fresh" is in cache
tr = Test.AddTestRun("0- fresh range cache check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://ident/fresh -r 0-', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache fresh")
tr.StillRunningAfter = ts

# 13 request with different etag and ensure it goes stale
tr = Test.AddTestRun("0- fresh range to stale")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://ident/fresh -r 0- -H 'X-Crr-Ident: Etag not_the_same'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

# 14 request with Last-Modified ensure it goes stale (expected etag)
tr = Test.AddTestRun("0- etag fresh range to stale")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f' http://ident/fresh -r 0- -H "X-Crr-Ident: Last-Modified {last_modified}"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

# 15 Test - Ensure Etag header match results in hit-fresh
tr = Test.AddTestRun("0- fresh ensure Etag check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://ident/fresh -r 0- -H 'X-Crr-Ident: Etag {etag_fresh}'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# 16 hit test asset to ensure its still fresh
tr = Test.AddTestRun("0- plain request path again")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://ident/fresh -r 0-', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# 17 request with force stale
tr = Test.AddTestRun("0- force stale")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://ident/fresh -r 0- -H 'X-Crr-Ident: Stale'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

## custom header

# 18 Test - Fetch custom asset into cache
tr = Test.AddTestRun("0- custom range cache load")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://identheader/custom -r 0-', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

# 19 Test - Ensure CrrIdent Etag header results in hit-fresh, custom header
tr = Test.AddTestRun("0- fresh CrrIdent Etag check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://identheader/custom -r 0- -H 'CrrIdent: Etag {etag_custom}'", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts
