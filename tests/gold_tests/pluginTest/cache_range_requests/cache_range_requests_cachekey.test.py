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
cache_range_requests with cachekey
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Reload remap rule with cache_range_requests plugin
# Request content through the cache_range_requests plugin

Test.SkipUnless(
    Condition.PluginExists('cache_range_requests.so'),
    Condition.PluginExists('cachekey.so'),
    Condition.PluginExists('xdebug.so'),
)
Test.ContinueOnFail = False
Test.testName = "cache_range_requests_cachekey"

# Define and configure ATS, enable traffic_ctl config reload
ts = Test.MakeATSProcess("ts", command="traffic_server")

# Define and configure origin server
server = Test.MakeOriginServer("server", lookup_key="{%uuid}")

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

# this request should work
req_full = {
    "headers": "GET /path HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "uuid: full\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

res_full = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + 'Etag: "foo"\r\n' +
        "Cache-Control: public, max-age=500\r\n" + "Connection: close\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_full, res_full)

# this request should work
req_good = {
    "headers":
        "GET /path HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" +
        "uuid: range_full\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

res_good = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + 'Etag: "foo"\r\n' +
        "Cache-Control: public, max-age=500\r\n" + "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) + "Connection: close\r\n" +
        "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_good, res_good)

# this request should fail with a cache_range_requests asset
req_fail = {
    "headers":
        "GET /path HTTP/1.1\r\n" + "Host: www.fail.com\r\n" + "Accept: */*\r\n" + "Range: bytes=0-\r\n" + "uuid: range_fail\r\n" +
        "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

res_fail = {
    "headers":
        "HTTP/1.1 206 Partial Content\r\n" + "Accept-Ranges: bytes\r\n" + 'Etag: "foo"\r\n' +
        "Cache-Control: public, max-age=500\r\n" + "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) + "Connection: close\r\n" +
        "\r\n",
    "timestamp": "1469733493.993",
    "body": body
}

server.addResponse("sessionlog.json", req_fail, res_fail)

# cache range requests plugin remap, working config
ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{}'.format(server.Variables.Port) +
    ' @plugin=cachekey.so @pparam=--include-headers=Range' + ' @plugin=cache_range_requests.so @pparam=--no-modify-cachekey',)

# improperly configured cache_range_requests with cachekey
ts.Disk.remap_config.AddLine(
    'map http://www.fail.com http://127.0.0.1:{}'.format(server.Variables.Port) + ' @plugin=cachekey.so @pparam=--static-prefix=foo'
    ' @plugin=cache_range_requests.so',)

# cache debug
ts.Disk.plugin_config.AddLine('xdebug.so')

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cache_range_requests',
})

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{} -H "x-debug: x-cache"'.format(ts.Variables.port)

# 0 Test - Fetch full asset into cache (ensure cold)
tr = Test.AddTestRun("full asset fetch")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://www.example.com/path -H "uuid: full"'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

# 1 Test - Fetch whole asset into cache via range request (ensure cold)
tr = Test.AddTestRun("0- asset fetch")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/path -r 0- -H "uuid: range_full"'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss for load")
tr.StillRunningAfter = ts

# 2 Test - Ensure assert happens instead of possible cache poisoning.
tr = Test.AddTestRun("Attempt poisoning")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.fail.com/path -r 0- -H "uuid: range_fail"'
ps.ReturnCode = 0
tr.StillRunningAfter = ts

ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR", "error condition hit")
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "Failed to change the cache url, disabling cache for this transaction to avoid cache poisoning.",
    "ensure failure for misconfiguration")
