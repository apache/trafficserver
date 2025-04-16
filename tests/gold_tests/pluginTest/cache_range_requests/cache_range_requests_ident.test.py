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
cache_range_requests X-Crr-Ident plugin test
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Ensure asset is stale and request with properly formed header.

Test.SkipUnless(
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('xdebug.so'),
)
#Test.ContinueOnFail = False
Test.ContinueOnFail = True
Test.testName = "cache_range_requests_ident"

# Define and configure ATS
ts = Test.MakeATSProcess("ts")

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

req_full = {
    "headers": "GET /path HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

last_modified = "Fri, 07 Mar 2025 18:06:58 GMT"
etag = '"772102f4-56f4bc1e6d417"'

res_full = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: max-age=1\r\n" +
        "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) + "Connection: close\r\n" + 'Etag: ' + etag + '\r\n' +
        'Last-Modified: ' + last_modified + '\r\n' + "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_full, res_full)

req_custom = {
    "headers": "GET /pathheader HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" + "\r\n",
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

# 0 Test - Fetch asset into cache
tr = Test.AddTestRun("0- range cache load")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(curl_and_args + ' http://ident/path -r 0-')
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

# 1 Test - Fetch asset into cache
tr = Test.AddTestRun("0- range cache load")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://identheader/pathheader -r 0-')
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

# 2 Test - Ensure range is fetched
tr = Test.AddTestRun("0- cache hit check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://ident/path -r 0-')
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit", "expected cache hit")
tr.StillRunningAfter = ts

# 3 Test - Ensure range is fetched
tr = Test.AddTestRun("0- cache hit check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://identheader/pathheader -r 0-')
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit", "expected cache hit")
tr.StillRunningAfter = ts

# These requests should flip from STALE to FRESH

# 4 Test - Ensure X-Crr-Ident Etag header results in hit-fresh
tr = Test.AddTestRun("0- range X-Crr-Ident Etag check")
tr.DelayStart = 2
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://ident/path -r 0- -H 'X-Crr-Ident: Etag: {etag}'")
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# 5 Test - Ensure X-Crr-Ident Etag header results in hit-fresh, custom header
tr = Test.AddTestRun("0- range CrrIdent Etag check")
tr.DelayStart = 2
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f" http://identheader/pathheader -r 0- -H 'CrrIdent: Etag: {etag_custom}'")
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# 6 Test - Ensure X-Crr-Ident Last-Modified header results in hit-fresh
tr = Test.AddTestRun("0- range X-Crr-Ident Last-Modified check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f' http://ident/path -r 0- -H "X-Crr-Ident: Last-Modified: {last_modified}"')
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# 7 Test - Provide a mismatch Etag force IMS request
tr = Test.AddTestRun("0- range X-Crr-Ident check")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + f' http://ident/path -r 0- -H "X-Crr-Ident: Last-Modified: foo"')
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

# post checks for traffic.out

ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    """Checking cached '"772102f4-56f4bc1e6d417"' against request 'Etag: "772102f4-56f4bc1e6d417"'""",
    "Etag is correctly considered")

ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    """Checking cached 'foo' against request 'Etag: foo'""", "Etag custom header is correctly considered")

ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    """Checking cached 'Fri, 07 Mar 2025 18:06:58 GMT' against request 'Last-Modified: Fri, 07 Mar 2025 18:06:58 GMT'""",
    "Last-Modified is correctly considered")
