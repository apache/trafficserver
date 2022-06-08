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

import time

Test.Summary = '''
cache_range_requests X-CRR-IMS plugin test
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Reload remap rule with cache_range_requests plugin
# Request content through the cache_range_requests plugin

Test.SkipUnless(
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = False
Test.testName = "cache_range_requests_ims"

# Define and configure ATS
ts = Test.MakeATSProcess("ts", command="traffic_server")

# Define and configure origin server
server = Test.MakeOriginServer("server")

# default root
req_chk = {"headers":
           "GET / HTTP/1.1\r\n" +
           "Host: www.example.com\r\n" +
           "uuid: none\r\n" +
           "\r\n",
           "timestamp": "1469733493.993",
           "body": ""
           }

res_chk = {"headers":
           "HTTP/1.1 200 OK\r\n" +
           "Connection: close\r\n" +
           "\r\n",
           "timestamp": "1469733493.993",
           "body": ""
           }

server.addResponse("sessionlog.json", req_chk, res_chk)

body = "lets go surfin now"
bodylen = len(body)

req_full = {"headers":
            "GET /path HTTP/1.1\r\n" +
            "Host: www.example.com\r\n" +
            "Accept: */*\r\n" +
            "Range: bytes=0-\r\n" +
            "\r\n",
            "timestamp": "1469733493.993",
            "body": ""
            }

res_full = {"headers":
            "HTTP/1.1 206 Partial Content\r\n" +
            "Accept-Ranges: bytes\r\n" +
            "Cache-Control: max-age=500\r\n" +
            "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) +
            "Connection: close\r\n" +
            'Etag: "772102f4-56f4bc1e6d417"\r\n' +
            "\r\n",
            "timestamp": "1469733493.993",
            "body": body
            }

server.addResponse("sessionlog.json", req_full, res_full)

# cache range requests plugin remap
ts.Disk.remap_config.AddLines([
    f'map http://ims http://127.0.0.1:{server.Variables.Port}' +
    ' @plugin=cache_range_requests.so @pparam=--consider-ims',
    f'map http://imsheader http://127.0.0.1:{server.Variables.Port}' +
    ' @plugin=cache_range_requests.so @pparam=--consider-ims' +
    ' @pparam=--ims-header=CrrIms',
])

# cache debug
ts.Disk.plugin_config.AddLine('xdebug.so')

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cache_range_requests',
})

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{} -H "x-debug: x-cache"'.format(ts.Variables.port)

# 0 Test - Fetch whole asset into cache
tr = Test.AddTestRun("0- range cache load")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://ims/path -r 0-'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts


# test inner range
# 1 Test - Fetch range into cache
tr = Test.AddTestRun("0- cache hit check")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://ims/path -r 0-'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit", "expected cache hit")
tr.StillRunningAfter = ts

# set up the IMS date field (go in the future) RFC 2616
futuretime = time.time() + 100  # seconds
futurestr = time.strftime("%a, %d %b %Y %H:%M:%S GMT", time.gmtime(futuretime))

# 2 Test - Ensure X-CRR-IMS header results in hit-stale
tr = Test.AddTestRun("0- range X-CRR-IMS check")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://ims/path -r 0- -H "X-CRR-IMS: {}"'.format(futurestr)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

futuretime += 10  # seconds
futurestr = time.strftime("%a, %d %b %Y %H:%M:%S GMT", time.gmtime(futuretime))

# 3 Test - Ensure CrrIms header results in hit-stale
tr = Test.AddTestRun("0- range CrrIms check, override header")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://imsheader/path -r 0- -H "CrrIms: {}"'.format(futurestr)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts
