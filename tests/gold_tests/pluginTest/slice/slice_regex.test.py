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
slice regex plugin test
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

# Define ATS and configure.
ts = Test.MakeATSProcess("ts", command="traffic_server")

# default root
request_header_chk = {"headers":
                      "GET / HTTP/1.1\r\n" +
                      "Host: www.example.com\r\n" +
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

#block_bytes = 7
body = "lets go surfin now"

request_header_txt = {"headers":
                      "GET /slice.txt HTTP/1.1\r\n" +
                      "Host: slice\r\n" +
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

response_header_txt = {"headers":
                       "HTTP/1.1 200 OK\r\n" +
                       "Connection: close\r\n" +
                       'Etag: "path"\r\n' +
                       "Cache-Control: max-age=500\r\n" +
                       "X-Info: notsliced\r\n" +
                       "\r\n",
                       "timestamp": "1469733493.993",
                       "body": body,
                       }

server.addResponse("sessionlog.json", request_header_txt, response_header_txt)

request_header_mp4 = {"headers":
                      "GET /slice.mp4 HTTP/1.1\r\n" +
                      "Host: sliced\r\n" +
                      "Range: bytes=0-99\r\n"
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

response_header_mp4 = {"headers":
                       "HTTP/1.1 206 Partial Content\r\n" +
                       "Connection: close\r\n" +
                       'Etag: "path"\r\n' +
                       "Content-Range: bytes 0-{}/{}\r\n".format(len(body) - 1, len(body)) +
                       "Cache-Control: max-age=500\r\n" +
                       "X-Info: sliced\r\n" +
                       "\r\n",
                       "timestamp": "1469733493.993",
                       "body": body,
                       }

server.addResponse("sessionlog.json", request_header_mp4, response_header_mp4)

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{} -H "x-debug: x-cache"'.format(ts.Variables.port)

block_bytes = 100

# set up whole asset fetch into cache
ts.Disk.remap_config.AddLines([
    'map http://exclude/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=slice.so' +
    ' @pparam=--blockbytes-test={}'.format(block_bytes) +
    ' @pparam=--exclude-regex=\\.txt'
    ' @pparam=--remap-host=sliced',
    'map http://include/ http://127.0.0.1:{}/'.format(server.Variables.Port) +
    ' @plugin=slice.so' +
    ' @pparam=--blockbytes-test={}'.format(block_bytes) +
    ' @pparam=--include-regex=\\.mp4'
    ' @pparam=--remap-host=sliced',
    'map http://sliced/ http://127.0.0.1:{}/'.format(server.Variables.Port),
])


# minimal configuration
ts.Disk.records_config.update({
    #  'proxy.config.diags.debug.enabled': 1,
    #  'proxy.config.diags.debug.tags': 'slice',
    'proxy.config.http.insert_age_in_response': 0,
    'proxy.config.http.response_via_str': 0,
})

# 0 Test - Exclude: ensure txt passes through
tr = Test.AddTestRun("Exclude - asset passed through")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://exclude/slice.txt'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Info: notsliced", "expected not sliced header")
tr.StillRunningAfter = ts

# 1 Test - Exclude mp4 gets sliced
tr = Test.AddTestRun("Exclude - asset is sliced")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://exclude/slice.mp4'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Info: sliced", "expected sliced header")
tr.StillRunningAfter = ts
tr.StillRunningAfter = ts

# 2 Test - Exclude: ensure txt passes through
tr = Test.AddTestRun("Include - asset passed through")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://include/slice.txt'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Info: notsliced", "expected not sliced header")
tr.StillRunningAfter = ts

# 3 Test - Exclude mp4 gets sliced
tr = Test.AddTestRun("Include - asset is sliced")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://include/slice.mp4'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Info: sliced", "expected sliced header")
tr.StillRunningAfter = ts
tr.StillRunningAfter = ts
