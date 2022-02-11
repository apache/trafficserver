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
Basic slice plugin test
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Reload remap rule with slice plugin
# Request content through the slice plugin

Test.SkipUnless(
    Condition.PluginExists('slice.so'),
)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_server")

# default root
request_header_chk = {"headers":
                      "GET / HTTP/1.1\r\n" +
                      "Host: ats\r\n" +
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

response_header_chk = {"headers":
                       "HTTP/1.1 200 OK\r\n" +
                       "Connection: close\r\n" +
                       "\r\n",
                       "timestamp": "1469733493.993",
                       "body": "",
                       }

server.addResponse("sessionlog.json", request_header_chk, response_header_chk)

block_bytes = 7
body = "lets go surfin now"

request_header = {"headers":
                  "GET /path HTTP/1.1\r\n" +
                  "Host: origin\r\n" +
                  "\r\n",
                  "timestamp": "1469733493.993",
                  "body": "",
                  }

response_header = {"headers":
                   "HTTP/1.1 200 OK\r\n" +
                   "Connection: close\r\n" +
                   'Etag: "path"\r\n' +
                   "Cache-Control: max-age=500\r\n" +
                   "\r\n",
                   "timestamp": "1469733493.993",
                   "body": body,
                   }

server.addResponse("sessionlog.json", request_header, response_header)

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x http://127.0.0.1:{}'.format(ts.Variables.port)

# set up whole asset fetch into cache
ts.Disk.remap_config.AddLines([
    f'map http://preload/ http://127.0.0.1:{server.Variables.Port}',
    f'map http://slice_only/ http://127.0.0.1:{server.Variables.Port}',
    f'map http://slice/ http://127.0.0.1:{server.Variables.Port}' +
    f' @plugin=slice.so @pparam=--blockbytes-test={block_bytes}',
    f'map http://slicehdr/ http://127.0.0.1:{server.Variables.Port}' +
    f' @plugin=slice.so @pparam=--blockbytes-test={block_bytes}' +
    ' @pparam=--skip-header=SkipSlice',
])

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'slice',
})

# 0 Test - Prefetch entire asset into cache
tr = Test.AddTestRun("Fetch first slice range")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://preload/path'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_200.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
tr.StillRunningAfter = ts

# 1 Test - First complete slice
tr = Test.AddTestRun("Fetch first slice range")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/path' + ' -r 0-6'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_first.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 0-6/18", "mismatch byte content response")
tr.StillRunningAfter = ts

# 2 Test - Last slice auto
tr = Test.AddTestRun("Last slice -- 14-")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/path' + ' -r 14-'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_last.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 14-17/18", "mismatch byte content response")
tr.StillRunningAfter = ts

# 3 Test - Last slice exact
tr = Test.AddTestRun("Last slice 14-17")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/path' + ' -r 14-17'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_last.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 14-17/18", "mismatch byte content response")
tr.StillRunningAfter = ts

# 4 Test - Last slice truncated
tr = Test.AddTestRun("Last truncated slice 14-20")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/path' + ' -r 14-20'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_last.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 14-17/18", "mismatch byte content response")
tr.StillRunningAfter = ts

# 5 Test - Whole asset via slices
tr = Test.AddTestRun("Whole asset via slices")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/path'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_200.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200 OK response")
tr.StillRunningAfter = ts

# 6 Test - Whole asset via range
tr = Test.AddTestRun("Whole asset via range")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/path' + ' -r 0-'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_206.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 0-17/18", "mismatch byte content response")
tr.StillRunningAfter = ts

# 7 Test - Non aligned slice request
tr = Test.AddTestRun("Non aligned slice request")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/path' + ' -r 5-16'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_mid.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 5-16/18", "mismatch byte content response")
tr.StillRunningAfter = ts

# 8 Test - special case, begin inside last slice block but outside asset len
tr = Test.AddTestRun("Invalid end range request, 416")
beg = len(body) + 1
end = beg + block_bytes
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/path' + ' -r {}-{}'.format(beg, end)
ps.Streams.stdout.Content = Testers.ContainsExpression("416 Requested Range Not Satisfiable", "expected 416 response")
tr.StillRunningAfter = ts

# 9 Test - First complete slice using override header
# if this fails it will infinite loop
tr = Test.AddTestRun("Fetch first slice range")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slicehdr/path' + ' -r 0-6'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/slice_first.stderr.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("206 Partial Content", "expected 206 response")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Range: bytes 0-6/18", "mismatch byte content response")
tr.StillRunningAfter = ts
