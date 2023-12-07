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
slice plugin prefetch feature test
'''

# Test description:
# Fill origin server with range requests
# Request content through slice plugin with varied prefetch counts

Test.SkipUnless(
    Condition.PluginExists('slice.so'),
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = False

# configure origin server, lookup by Range header
server = Test.MakeOriginServer("server", lookup_key="{%Range}")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_server")

block_bytes_1 = 7
block_bytes_2 = 5
body = "lets go surfin now"
bodylen = len(body)

request_header = {
    "headers": "GET /path HTTP/1.1\r\n" + "Host: origin\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

response_header = {
    "headers": "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "Cache-Control: public, max-age=5\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": body,
}

server.addResponse("sessionlog.json", request_header, response_header)

# Autest OS doesn't support range request, must manually add requests/responses
for block_bytes in [block_bytes_1, block_bytes_2]:
    for i in range(bodylen // block_bytes + 1):
        b0 = i * block_bytes
        b1 = b0 + block_bytes - 1
        req_header = {
            "headers": "GET /path HTTP/1.1\r\n" + "Host: *\r\n" + "Accept: */*\r\n" + f"Range: bytes={b0}-{b1}\r\n" + "\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        if (b1 > bodylen - 1):
            b1 = bodylen - 1
        resp_header = {
            "headers":
                "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + "Cache-Control: public, max-age=5\r\n" +
                f"Content-Range: bytes {b0}-{b1}/{bodylen}\r\n" + "Connection: close\r\n" + "\r\n",
            "timestamp": "1469733493.993",
            "body": body[b0:b1 + 1]
        }
        server.addResponse("sessionlog.json", req_header, resp_header)

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x http://127.0.0.1:{} -H "x-debug: x-cache"'.format(ts.Variables.port)

ts.Disk.remap_config.AddLines(
    [
        f'map http://sliceprefetchbytes1/ http://127.0.0.1:{server.Variables.Port}' +
        f' @plugin=slice.so @pparam=--blockbytes-test={block_bytes_1} @pparam=--prefetch-count=1 \\' +
        ' @plugin=cache_range_requests.so',
        f'map http://sliceprefetchbytes2/ http://127.0.0.1:{server.Variables.Port}' +
        f' @plugin=slice.so @pparam=--blockbytes-test={block_bytes_2} @pparam=--prefetch-count=3 \\' +
        ' @plugin=cache_range_requests.so',
    ])

ts.Disk.plugin_config.AddLine('xdebug.so')
ts.Disk.logging_yaml.AddLines(
    [
        'logging:',
        '  formats:',
        '  - name: cache',
        '    format: "%<{Content-Range}psh> %<{X-Cache}psh>"',
        '  logs:',
        '    - filename: cache',
        '      format: cache',
        '      mode: ascii',
    ])

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'slice|cache_range_requests|xdebug',
    })

# 0 Test - Full object slice (miss) with only block 14-20 prefetched in background, block bytes= 7
tr = Test.AddTestRun("Full object slice: first block is miss, only block 14-20 prefetched")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://sliceprefetchbytes1/path'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_200.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
ps.Streams.stdout.Content += Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
tr.StillRunningAfter = ts

# 1 Test - Full object slice (hit-fresh) with no prefetched blocks, block bytes= 7
tr = Test.AddTestRun("Full object slice: first block is hit-fresh, no blocks prefetched")
ps = tr.Processes.Default
ps.Command = 'sleep 1; ' + curl_and_args + ' http://sliceprefetchbytes1/path'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_200.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
ps.Streams.stdout.Content += Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache fresh")
tr.StillRunningAfter = ts

# 2 Test - Full object slice with only next block prefetched in background, block bytes= 7
tr = Test.AddTestRun("Full object slice: first block is hit-stale, only block 14-20 prefetched")
ps = tr.Processes.Default
ps.Command = 'sleep 5; ' + curl_and_args + ' http://sliceprefetchbytes1/path'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_200.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
ps.Streams.stdout.Content += Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit-stale")
tr.StillRunningAfter = ts

# 3 Test - Full object slice (hit-fresh) with no prefetched blocks, block bytes= 7
tr = Test.AddTestRun("Full object slice: first block is hit-fresh with range 0-, no blocks prefetched")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://sliceprefetchbytes1/path' + ' -r 0-'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_200.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 0-17/18", "mismatch byte content response")
ps.Streams.stdout.Content += Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit-fresh")
tr.StillRunningAfter = ts

# 4 Test - Client range request (hit-stale/miss) enables prefetching
tr = Test.AddTestRun("Client range request")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://sliceprefetchbytes2/path' + ' -r 5-16'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_mid.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 5-16/18", "mismatch byte content response")
ps.Streams.stdout.Content += Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
tr.StillRunningAfter = ts

# 5 Test - special case, begin inside last slice block but outside asset len
tr = Test.AddTestRun("Invalid end range request, 416")
beg = len(body) + 1
end = beg + block_bytes
ps = tr.Processes.Default
ps.Command = curl_and_args + f' http://sliceprefetchbytes1/path -r {beg}-{end}'
ps.Streams.stdout.Content = Testers.ContainsExpression("416 Requested Range Not Satisfiable", "expected 416 response")
tr.StillRunningAfter = ts

# 6 Test - All requests (client & slice internal) logs to see background fetches
cache_file = os.path.join(ts.Variables.LOGDIR, 'cache.log')
# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
test_run = Test.AddTestRun("Checking debug logs for background fetches")
test_run.Processes.Default.Command = (os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' + cache_file)
ts.Disk.File(cache_file).Content = "gold/slice_prefetch.gold"
test_run.Processes.Default.ReturnCode = 0
