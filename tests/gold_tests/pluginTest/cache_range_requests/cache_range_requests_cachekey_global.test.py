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
cache_range_requests with cachekey as a global plugin
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
Test.testName = "cache_range_requests_cachekey_global"

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
ts.Disk.remap_config.AddLine('map http://www.example.com http://127.0.0.1:{}'.format(server.Variables.Port))

# improperly configured cache_range_requests with cachekey
ts.Disk.remap_config.AddLine('map http://www.fail.com http://127.0.0.1:{}'.format(server.Variables.Port))

# cache debug
ts.Disk.plugin_config.AddLines(
    [
        'cachekey.so --include-headers=Range --static-prefix=foo',
        'cache_range_requests.so --no-modify-cachekey',
        'xdebug.so',
    ])

# minimal configuration
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'cachekey|cache_range_requests',
    })

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{} -H "x-debug: x-cache-key"'.format(ts.Variables.port)

# 0 Test - Fetch full asset via range
tr = Test.AddTestRun("asset fetch via range")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://www.example.com/path -r0- -H "uuid: full"'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "X-Cache-Key: /foo/Range:bytes=0-/path", "expected cachekey style range request in cachekey")
tr.StillRunningAfter = ts
