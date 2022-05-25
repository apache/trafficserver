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
cache_range_requests cache-complete-responses test
'''

# Test description:
# Two rounds of testing:
#  Round 1:
#   - seed the cache with an object that is smaller than the slice block size
#   - issue requests with various ranges and validate responses are 200s
#  Round 2:
#   - seed the cache with an object that is larger than the slice block size
#   - issue requests with various ranges and validate responses are 206s
# Both rounds test cache miss, cache hit, and refresh hit scenarios
# Use the cachekey plugin to add the `Range` request header to the cache key
# Request content through the slice and cache_range_requests plugin with a 4MB slice block size

Test.SkipUnless(
    Condition.PluginExists('cachekey.so'),
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('slice.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = False
Test.testName = "cache_range_requests_cache_200s"

# Generate bodies for our responses
small_body_len = 10000
small_body = ''
for i in range(small_body_len):
    small_body += 'x'

slice_body_len = 4 * 1024 * 1024
slice_body = ''
for i in range(slice_body_len):
    slice_body += 'x'

# Define and configure ATS
ts = Test.MakeATSProcess("ts", command="traffic_server")

# Define and configure origin server
server = Test.MakeOriginServer("server", lookup_key="{%UID}")

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

small_req = {"headers":
             "GET /obj HTTP/1.1\r\n" +
             "Host: www.example.com\r\n" +
             "Accept: */*\r\n" +
             "UID: SMALL\r\n"
             "\r\n",
             "timestamp": "1469733493.993",
             "body": ""
             }

small_resp = {"headers":
              "HTTP/1.1 200 OK\r\n" +
              "Cache-Control: max-age=1\r\n" +
              "Connection: close\r\n" +
              'Etag: "772102f4-56f4bc1e6d417"\r\n' +
              "\r\n",
              "timestamp": "1469733493.993",
              "body": small_body
              }

small_reval_req = {"headers":
                   "GET /obj HTTP/1.1\r\n" +
                   "Host: www.example.com\r\n" +
                   "Accept: */*\r\n" +
                   "UID: SMALL-INM\r\n"
                   "\r\n",
                   "timestamp": "1469733493.993",
                   "body": ""
                   }

small_reval_resp = {"headers":
                    "HTTP/1.1 304 Not Modified\r\n" +
                    "Cache-Control: max-age=10\r\n" +
                    "Connection: close\r\n" +
                    'Etag: "772102f4-56f4bc1e6d417"\r\n' +
                    "\r\n",
                    "timestamp": "1469733493.993"
                    }

slice_req = {"headers":
             "GET /slice HTTP/1.1\r\n" +
             "Host: www.example.com\r\n" +
             "Range: bytes=0-4194303\r\n" +
             "Accept: */*\r\n" +
             "UID: SLICE\r\n"
             "\r\n",
             "timestamp": "1469733493.993",
             }

slice_resp = {"headers":
              "HTTP/1.1 206 Partial Content\r\n" +
              "Cache-Control: max-age=1\r\n" +
              "Content-Range: bytes 0-{}/{}\r\n".format(slice_body_len - 1, slice_body_len * 2) + "\r\n" +
              "Content-Length: {}\r\n".format(slice_body_len) + "\r\n" +
              "Connection: close\r\n" +
              'Etag: "872104f4-d6bcaa1e6f979"\r\n' +
              "\r\n",
              "timestamp": "1469733493.993",
              "body": slice_body
              }

slice_reval_req = {"headers":
                   "GET /slice HTTP/1.1\r\n" +
                   "Host: www.example.com\r\n" +
                   "Accept: */*\r\n" +
                   "UID: SLICE-INM\r\n"
                   "\r\n",
                   "timestamp": "1469733493.993",
                   "body": ""
                   }

slice_reval_resp = {"headers":
                    "HTTP/1.1 304 Not Modified\r\n" +
                    "Cache-Control: max-age=10\r\n" +
                    "Connection: close\r\n" +
                    'Etag: "872104f4-d6bcaa1e6f979"\r\n' +
                    "\r\n",
                    "timestamp": "1469733493.993"
                    }

server.addResponse("sessionlog.json", small_req, small_resp)
server.addResponse("sessionlog.json", small_reval_req, small_reval_resp)
server.addResponse("sessionlog.json", slice_req, slice_resp)
server.addResponse("sessionlog.json", slice_reval_req, slice_reval_resp)

# remap with slice, cachekey, and the cache range requests plugin
ts.Disk.remap_config.AddLines([
    f'map http://example.com http://127.0.0.1:{server.Variables.Port} \\' +
    ' @plugin=slice.so @pparam=--blockbytes=4m \\',
    ' @plugin=cachekey.so @pparam=--key-type=cache_key @pparam=--include-headers=Range @pparam=--remove-all-params=true \\',
    ' @plugin=cache_range_requests.so @pparam=--no-modify-cachekey @pparam=--cache-complete-responses',
])

# cache debug
ts.Disk.plugin_config.AddLine('xdebug.so')

# enable debug
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cachekey|cache_range_requests|slice',
})

# base cURL command
curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{} -H "x-debug: x-cache"'.format(ts.Variables.port)

# Test round 1: ensure we fetch and cache objects that are returned with a
# 200 OK and no Content-Range when the object is smaller than the slice
# block size

# 0 Test - Fetch /obj with a Range header but less than 4MB
tr = Test.AddTestRun("cache miss on /obj")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' -H "UID: SMALL" http://example.com/obj -r 0-5000'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK")
ps.Streams.stdout.Content = Testers.ExcludesExpression("Content-Range:", "expected no Content-Range header")
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss, none", "expected cache miss")
tr.StillRunningAfter = ts

# 1 Test - Fetch /obj with a different range but less than 4MB
tr = Test.AddTestRun("cache hit-fresh on /obj")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' -H "UID: SMALL" http://example.com/obj -r 5001-5999'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK")
ps.Streams.stdout.Content = Testers.ExcludesExpression("Content-Range:", "expected no Content-Range header")
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh, none", "expected cache hit")
tr.StillRunningAfter = ts

# 2 Test - Revalidate /obj with a different range but less than 4MB
tr = Test.AddTestRun("cache hit-stale on /obj")
tr.DelayStart = 2
ps = tr.Processes.Default
ps.Command = curl_and_args + ' -H "UID: SMALL-INM" http://example.com/obj -r 0-403'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK")
ps.Streams.stdout.Content = Testers.ExcludesExpression("Content-Range:", "expected no Content-Range header")
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale, none", "expected cache hit stale")
tr.StillRunningAfter = ts

# 3 Test - Fetch /obj with a different range but less than 4MB
tr = Test.AddTestRun("cache hit on /obj post revalidation")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' -H "UID: SMALL" http://example.com/obj -r 0-3999'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK")
ps.Streams.stdout.Content = Testers.ExcludesExpression("Content-Range:", "expected no Content-Range header")
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh, none", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# Test round 2: repeat, but ensure we have 206s and matching Content-Range
# headers due to a base object that exceeds the slice block size

# 4 Test - Fetch /slice with a Range header but less than 4MB
tr = Test.AddTestRun("cache miss on /slice")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' -H "UID: SLICE" http://example.com/slice -r 0-5000'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 Partial Content")
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "Content-Range: bytes 0-5000/8388608",
    "expected Content-Range: bytes 0-5000/8388608")
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss, none", "expected cache miss")
tr.StillRunningAfter = ts

# 5 Test - Fetch /slice with a different range but less than 4MB
tr = Test.AddTestRun("cache hit-fresh on /slice")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' -H "UID: SLICE" http://example.com/slice -r 5001-5999'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 Partial Content")
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "Content-Range: bytes 5001-5999/8388608",
    "expected Content-Range: bytes 5001-5999/8388608")
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh, none", "expected cache hit")
tr.StillRunningAfter = ts

# 6 Test - Revalidate /slice with a different range but less than 4MB
tr = Test.AddTestRun("cache hit-stale on /slice")
tr.DelayStart = 2
ps = tr.Processes.Default
ps.Command = curl_and_args + ' -H "UID: SLICE-INM" http://example.com/slice -r 0-403'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 Partial Content")
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "Content-Range: bytes 0-403/8388608",
    "expected Content-Range: bytes 0-403/8388608")
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale, none", "expected cache hit stale")
tr.StillRunningAfter = ts

# 7 Test - Fetch /slice with a different range but less than 4MB
tr = Test.AddTestRun("cache hit on /slice post revalidation")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' -H "UID: SLICE" http://example.com/slice -r 0-3999'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 Partial Content")
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "Content-Range: bytes 0-3999/8388608",
    "expected Content-Range: bytes 0-3999/8388608")
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh, none", "expected cache hit-fresh")
tr.StillRunningAfter = ts
