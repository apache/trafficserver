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
ts = Test.MakeATSProcess("ts", command="traffic_server")

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

req_full = {"headers":
            "GET /path HTTP/1.1\r\n" +
            "Host: www.example.com\r\n" +
            "Accept: */*\r\n" +
            "uuid: full\r\n" +
            "\r\n",
            "timestamp": "1469733493.993",
            "body": ""
            }

res_full = {"headers":
            "HTTP/1.1 200 OK\r\n" +
            "Cache-Control: max-age=500\r\n" +
            "Connection: close\r\n" +
            'Etag: "path"\r\n' +
            "\r\n",
            "timestamp": "1469733493.993",
            "body": body
            }

server.addResponse("sessionlog.json", req_full, res_full)

block_bytes = 7
bodylen = len(body)

inner_str = "7-15"

req_inner = {"headers":
             "GET /path HTTP/1.1\r\n" +
             "Host: www.example.com\r\n" +
             "Accept: */*\r\n" +
             "Range: bytes={}\r\n".format(inner_str) +
             "uuid: inner\r\n" +
             "\r\n",
             "timestamp": "1469733493.993",
             "body": ""
             }

res_inner = {"headers":
             "HTTP/1.1 206 Partial Content\r\n" +
             "Accept-Ranges: bytes\r\n" +
             "Cache-Control: max-age=500\r\n" +
             "Content-Range: bytes {0}/{1}\r\n".format(inner_str, bodylen) +
             "Connection: close\r\n" +
             'Etag: "path"\r\n' +
             "\r\n",
             "timestamp": "1469733493.993",
             "body": body[7:15]
             }

server.addResponse("sessionlog.json", req_inner, res_inner)

frange_str = "0-"

req_frange = {"headers":
              "GET /path HTTP/1.1\r\n" +
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

last_str = "-5"

req_last = {"headers":
            "GET /path HTTP/1.1\r\n" +
            "Host: www.example.com\r\n" +
            "Accept: */*\r\n" +
            "Range: bytes={}\r\n".format(last_str) +
            "uuid: last\r\n" +
            "\r\n",
            "timestamp": "1469733493.993",
            "body": ""
            }

res_last = {"headers":
            "HTTP/1.1 206 Partial Content\r\n" +
            "Accept-Ranges: bytes\r\n" +
            "Cache-Control: max-age=200\r\n" +
            "Content-Range: bytes {0}-{1}/{1}\r\n".format(bodylen - 5, bodylen) +
            "Connection: close\r\n" +
            'Etag: "path"\r\n' +
            "\r\n",
            "timestamp": "1469733493.993",
            "body": body[-5:]
            }

server.addResponse("sessionlog.json", req_last, res_last)

pselect_str = "1-10"

req_pselect = {"headers":
               "GET /path HTTP/1.1\r\n" +
               "Host: parentselect\r\n" +
               "Accept: */*\r\n" +
               "Range: bytes={}\r\n".format(pselect_str) +
               "uuid: pselect\r\n" +
               "\r\n",
               "timestamp": "1469733493.993",
               "body": ""
               }

res_pselect = {"headers":
               "HTTP/1.1 206 Partial Content\r\n" +
               "Accept-Ranges: bytes\r\n" +
               "Cache-Control: max-age=200\r\n" +
               "Content-Range: bytes {}/19\r\n".format(pselect_str) +
               "Connection: close\r\n" +
               'Etag: "path"\r\n' +
               "\r\n",
               "timestamp": "1469733493.993",
               "body": body[1:10]
               }

server.addResponse("sessionlog.json", req_pselect, res_pselect)

req_psd = {"headers":
           "GET /path HTTP/1.1\r\n" +
           "Host: psd\r\n" +
           "Accept: */*\r\n" +
           "Range: bytes={}\r\n".format(pselect_str) +
           "uuid: pselect\r\n" +
           "\r\n",
           "timestamp": "1469733493.993",
           "body": ""
           }

server.addResponse("sessionlog.json", req_psd, res_pselect)

# cache range requests plugin remap
ts.Disk.remap_config.AddLines([
    'map http://www.example.com http://127.0.0.1:{}'.format(server.Variables.Port) +
    ' @plugin=cache_range_requests.so',

    # parent select cache key option
    'map http://parentselect http://127.0.0.1:{}'.format(server.Variables.Port) +
    ' @plugin=cache_range_requests.so @pparam=--ps-cachekey',

    # deprecated
    'map http://psd http://127.0.0.1:{}'.format(server.Variables.Port) +
    ' @plugin=cache_range_requests.so @pparam=ps_mode:cache_key_url',
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
tr = Test.AddTestRun("full asset cache miss bypass")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://www.example.com/path -H "uuid: full"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/full.stderr.gold"
tr.StillRunningAfter = ts

# test inner range
# 1 Test - Fetch range into cache
tr = Test.AddTestRun("inner range cache miss")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/path -r {} -H "uuid: inner"'.format(inner_str)
ps.ReturnCode = 0
ps.Streams.stderr = "gold/inner.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 7-15/18", "expected content-range header")
tr.StillRunningAfter = ts

# 2 Test - Fetch from cache
tr = Test.AddTestRun("inner range cache hit")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/path -r {}'.format(inner_str)
ps.ReturnCode = 0
ps.Streams.stderr = "gold/inner.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit", "expected cache hit")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 7-15/18", "expected content-range header")
tr.StillRunningAfter = ts

# full range

# 3 Test - 0- request
tr = Test.AddTestRun("0- request miss")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/path -r {} -H "uuid: frange"'.format(frange_str)
ps.ReturnCode = 0
ps.Streams.stderr = "gold/full.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 0-18/18", "expected content-range header")
tr.StillRunningAfter = ts

# 4 Test - 0- request
tr = Test.AddTestRun("0- request hit")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/path -r {}'.format(frange_str)
ps.ReturnCode = 0
ps.Streams.stderr = "gold/full.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit", "expected cache hit")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 0-18/18", "expected content-range header")
tr.StillRunningAfter = ts

# end range

# 5 Test - -5 request miss
tr = Test.AddTestRun("-5 request miss")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/path -r {} -H "uuid: last"'.format(last_str)
ps.ReturnCode = 0
ps.Streams.stderr = "gold/last.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 13-18/18", "expected content-range header")
tr.StillRunningAfter = ts

# 6 Test - -5 request hit
tr = Test.AddTestRun("-5 request hit")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/path -r {}'.format(last_str)
ps.ReturnCode = 0
ps.Streams.stderr = "gold/last.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit", "expected cache hit")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 13-18/18", "expected content-range header")
tr.StillRunningAfter = ts

# Ensure 404's aren't getting cached

# 7 Test - 404
tr = Test.AddTestRun("404 request 1st")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/404 -r 0-'
ps.Streams.stdout = "gold/404.stdout.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
tr.StillRunningAfter = ts

# 8 Test - 404
tr = Test.AddTestRun("404 request 2nd")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/404 -r 0-'
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss")
ps.Streams.stdout.Content += Testers.ContainsExpression("404 Not Found", "expected 404 response")

tr.StillRunningAfter = ts

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{} -H "x-debug: x-parentselection-key"'.format(
    ts.Variables.port)

# 9 Test - cache_key_url request
tr = Test.AddTestRun("cache_key_url request")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://parentselect/path -r {} -H "uuid: pselect"'.format(pselect_str)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "X-ParentSelection-Key: .*-bytes=",
    "expected bytes in parent selection key",
)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 10 Test - non cache_key_url request ... no X-ParentSelection-Key
tr = Test.AddTestRun("non cache_key_url request")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://www.example.com/path -r {} -H "uuid: inner"'.format(inner_str)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ExcludesExpression("X-ParentSelection-Key", "parent select key shouldn't show up")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# 11 Test - cache_key_url request -- deprecated
tr = Test.AddTestRun("cache_key_url request - dprecated")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://psd/path -r {} -H "uuid: pselect"'.format(pselect_str)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression(
    "X-ParentSelection-Key: .*-bytes=",
    "expected bytes in parent selection key",
)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
