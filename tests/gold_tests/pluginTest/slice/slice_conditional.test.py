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

Test.Summary = '''
Conditional Slicing test
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Reload remap rule with slice plugin
# Request content through the slice plugin

Test.SkipUnless(
    Condition.PluginExists('slice.so'),
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server", lookup_key="{PATH}{%Range}", options={'-v': None})

# Define ATS and configure
ts = Test.MakeATSProcess("ts")

# small object, should not be sliced
req_small = {
    "headers": "GET /small HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "\r\n",
    "body": "",
}
res_small = {
    "headers": "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "Cache-Control: max-age=10,public\r\n" + "\r\n",
    "body": "smol",
}
server.addResponse("sessionlog.json", req_small, res_small)

large_body = "large object!"
# large object, all in one slice
req_large = {
    "headers": "GET /large HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "\r\n",
    "body": "",
}
res_large = {
    "headers": "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "Cache-Control: max-age=10,public\r\n" + "\r\n",
    "body": large_body
}
server.addResponse("sessionlog.json", req_large, res_large)

# large object, this populates the individual slices in the server

body_len = len(large_body)
slice_begin = 0
slice_end = 0
slice_block_size = 10
while (slice_end < body_len):
    # 1st slice
    slice_end = slice_begin + slice_block_size
    req_large_slice = {
        "headers": "GET /large HTTP/1.1\r\n" + "Host: www.example.com\r\n" + f"Range: bytes={slice_begin}-{slice_end - 1}" + "\r\n",
        "body": "",
    }
    if slice_end > body_len:
        slice_end = body_len
    res_large_slice = {
        "headers":
            "HTTP/1.1 206 Partial Content\r\n" + "Connection: close\r\n" + "Accept-Ranges: bytes\r\n" +
            f"Content-Range: bytes {slice_begin}-{slice_end - 1}/{body_len}\r\n" + "Cache-Control: max-age=10,public\r\n" + "\r\n",
        "body": large_body[slice_begin:slice_end]
    }
    server.addResponse("sessionlog.json", req_large_slice, res_large_slice)
    slice_begin = slice_end

# set up slice plugin with remap host into cache_range_requests
ts.Disk.remap_config.AddLines(
    [
        f'map http://slice/ http://127.0.0.1:{server.Variables.Port}/' +
        f' @plugin=slice.so @pparam=--blockbytes-test={slice_block_size} @pparam=--minimum-size=8 @pparam=--metadata-cache-size=4 @plugin=cache_range_requests.so'
    ])

ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': '1',
        'proxy.config.diags.debug.tags': 'http|cache|slice|xdebug',
    })

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{}'.format(ts.Variables.port) + ' -H "x-debug: x-cache"'

# Test case 1: first request of small object
tr = Test.AddTestRun("Small request 1")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://slice/small'
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression('smol', 'expected smol')
ps.Streams.stdout.Content = Testers.ContainsExpression('X-Cache: miss', 'expected cache miss')
tr.StillRunningAfter = ts

# Test case 2: second request of small object - expect cache hit
tr = Test.AddTestRun("Small request 2")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/small'
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression('smol', 'expected smol')
ps.Streams.stdout.Content = Testers.ContainsExpression('X-Cache: hit-fresh', 'expected cache hit-fresh')
tr.StillRunningAfter = ts

# Test case 3: first request of large object - expect unsliced, cache write disabled
tr = Test.AddTestRun("Large request 1")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/large'
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression('large object!', 'expected large object')
ps.Streams.stdout.Content = Testers.ContainsExpression('X-Cache: miss', 'expected cache miss')
tr.StillRunningAfter = ts

# Test case 4: first request of large object - expect sliced, cache miss
tr = Test.AddTestRun("Large request 2")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/large'
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression('large object!', 'expected large object')
ps.Streams.stdout.Content = Testers.ContainsExpression('X-Cache: miss', 'expected cache miss')
tr.StillRunningAfter = ts

## Test case 4: first request of large object - expect cache hit
#tr = Test.AddTestRun("Large request 3")
#ps = tr.Processes.Default
#ps.Command = curl_and_args + ' http://slice/large'
#ps.ReturnCode = 0
#ps.Streams.stderr.Content = Testers.ContainsExpression('large object!', 'expected large object')
#ps.Streams.stdout.Content = Testers.ContainsExpression('X-Cache: hit-fresh', 'expected cache hit-fresh')
#tr.StillRunningAfter = ts
