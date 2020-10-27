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
Basic cache_range_requests plugin test
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
Test.testName = "cache_range_requests"

# Define and configure ATS
ts = Test.MakeATSProcess("ts", command="traffic_server", enable_cache=False)

# Define and configure origin server
server = Test.MakeOriginServer("server", lookup_key="{%uuid}")

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
frange_str = "0-"

req_frange = {"headers":
              "GET /path~foo HTTP/1.1\r\n" +
              "Host: www.example.com\r\n" +
              "Accept: */*\r\n" +
              "Range: bytes={}\r\n".format(frange_str) +
              "uuid: frange\r\n" +
              "\r\n",
              "timestamp": "1469733493.993",
              "body": ""
              }

res_frange = {"headers":
              "HTTP/1.1 206 Partial Content\r\n" +
              "Accept-Ranges: bytes\r\n" +
              "Cache-Control: max-age=500\r\n" +
              "Content-Range: bytes 0-{0}/{0}\r\n".format(bodylen) +
              "Connection: close\r\n" +
              'Etag: "path"\r\n' +
              "\r\n",
              "timestamp": "1469733493.993",
              "body": body
              }

server.addResponse("sessionlog.json", req_frange, res_frange)

# cache range requests plugin remap
ts.Disk.remap_config.AddLines([
    'map http://crr/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=cache_range_requests.so',

    'map http://cachekey_crr/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=cachekey.so @pparam=--canonical-prefix @pparam=--static-prefix=http://127.0.0.1:{}'.format(server.Variables.Port)
    + ' @pparam=--crr-compat-range @pparam=--percent-encode=false @plugin=cache_range_requests.so @pparam=--no-modify-cachekey'
])

# cache debug
ts.Disk.plugin_config.AddLine('xdebug.so')

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'cache_range_requests',
})

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{} -H "x-debug: X-Cache-Key"'.format(ts.Variables.port)

cachekey_exp = 'http://127.0.0.1:{}/path~foo-bytes=0-'.format(server.Variables.Port)

# 0 Test - 0- request cache_range_requests
tr = Test.AddTestRun("0- request miss")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' "http://crr/path~foo" -r {} -H "uuid: frange"'.format(frange_str)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache-Key: " + cachekey_exp, "expected cache miss")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 0-18/18", "expected content-range header")
tr.StillRunningAfter = ts

# 4 Test - 0- request using cachekey
tr = Test.AddTestRun("0- request hit")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' "http://cachekey_crr/path~foo" -r {} -H "uuid: frange"'.format(frange_str)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache-Key: " + cachekey_exp, "expected cache miss")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 0-18/18", "expected content-range header")
tr.StillRunningAfter = ts

# end range
