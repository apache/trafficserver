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
import datetime

Test.Summary = '''
Slice selfhealing test
'''


def to_httpdate(dt):
    # string representation of a date according to RFC 1123 (HTTP/1.1).
    weekday = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"][dt.weekday()]
    month = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"][dt.month - 1]
    return "%s, %02d %s %04d %02d:%02d:%02d GMT" % (weekday, dt.day, month,
                                                    dt.year, dt.hour, dt.minute, dt.second)

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
server = Test.MakeOriginServer("server", lookup_key="{%uuid}")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_server")

# default root
req_header_chk = {"headers":
                  "GET / HTTP/1.1\r\n" +
                  "Host: www.example.com\r\n" +
                  "uuid: none\r\n" +
                  "\r\n",
                  "timestamp": "1469733493.993",
                  "body": "",
                  }

res_header_chk = {"headers":
                  "HTTP/1.1 200 OK\r\n" +
                  "Connection: close\r\n" +
                  "\r\n",
                  "timestamp": "1469733493.993",
                  "body": "",
                  }

server.addResponse("sessionlog.json", req_header_chk, res_header_chk)

# set up slice plugin with remap host into cache_range_requests
ts.Disk.remap_config.AddLines([
    f'map http://slice/ http://127.0.0.1:{server.Variables.Port}/' +
    ' @plugin=slice.so @pparam=--blockbytes-test=3 @pparam=--remap-host=crr',
    f'map http://crr/ http://127.0.0.1:{server.Variables.Port}/' +
    '  @plugin=cache_range_requests.so @pparam=--consider-ims',
    f'map http://slicehdr/ http://127.0.0.1:{server.Variables.Port}/' +
    ' @plugin=slice.so @pparam=--blockbytes-test=3' +
    ' @pparam=--remap-host=crrhdr @pparam=--crr-ims-header=crr-foo',
    f'map http://crrhdr/ http://127.0.0.1:{server.Variables.Port}/'
    '  @plugin=cache_range_requests.so @pparam=--ims-header=crr-foo',
])

ts.Disk.plugin_config.AddLine('xdebug.so')

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'cache_range_requests|slice',
})

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x localhost:{}'.format(ts.Variables.port) + ' -H "x-debug: x-cache"'

# Test case: 2nd slice out of date (refetch and continue)

req_header_2ndold1 = {"headers":
                      "GET /second HTTP/1.1\r\n" +
                      "Host: www.example.com\r\n" +
                      "uuid: etagold-1\r\n" +
                      "Range: bytes=3-5\r\n"
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

res_header_2ndold1 = {"headers":
                      "HTTP/1.1 206 Partial Content\r\n" +
                      "Accept-Ranges: bytes\r\n" +
                      "Cache-Control: max-age=5000\r\n" +
                      "Connection: close\r\n" +
                      "Content-Range: bytes 3-4/5\r\n" +
                      'Etag: "etagold"\r\n' +
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "aa"
                      }

server.addResponse("sessionlog.json", req_header_2ndold1, res_header_2ndold1)

req_header_2ndnew0 = {"headers":
                      "GET /second HTTP/1.1\r\n" +
                      "Host: www.example.com\r\n" +
                      "uuid: etagnew-0\r\n" +
                      "Range: bytes=0-2\r\n"
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

res_header_2ndnew0 = {"headers":
                      "HTTP/1.1 206 Partial Content\r\n" +
                      "Accept-Ranges: bytes\r\n" +
                      "Cache-Control: max-age=5000\r\n" +
                      "Connection: close\r\n" +
                      "Content-Range: bytes 0-2/5\r\n" +
                      'Etag: "etagnew"\r\n' +
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "bbb"
                      }

server.addResponse("sessionlog.json", req_header_2ndnew0, res_header_2ndnew0)

req_header_2ndnew1 = {"headers":
                      "GET /second HTTP/1.1\r\n" +
                      "Host: www.example.com\r\n" +
                      "uuid: etagnew-1\r\n" +
                      "Range: bytes=3-5\r\n"
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

res_header_2ndnew1 = {"headers":
                      "HTTP/1.1 206 Partial Content\r\n" +
                      "Accept-Ranges: bytes\r\n" +
                      "Cache-Control: max-age=5000\r\n" +
                      "Connection: close\r\n" +
                      "Content-Range: bytes 3-4/5\r\n" +
                      'Etag: "etagnew"\r\n' +
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "bb"
                      }

server.addResponse("sessionlog.json", req_header_2ndnew1, res_header_2ndnew1)

# 0 Test - Preload reference etagnew-0
tr = Test.AddTestRun("Preload reference etagnew-0")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://crr/second -r 0-2 -H "uuid: etagnew-0"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/bbb.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etagnew", "expected etagnew")
tr.StillRunningAfter = ts

# 1 Test - Preload slice etagold-1
tr = Test.AddTestRun("Preload slice etagold-1")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://crr/second -r 3-5 -H "uuid: etagold-1"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/aa.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etagold", "expected etagold")
tr.StillRunningAfter = ts

# 2 Test - Request second slice via slice plugin, with instructions to fetch new 2nd slice
tr = Test.AddTestRun("Request 2nd slice (expect refetch)")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/second -r 3- -H "uuid: etagnew-1"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/bb.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etagnew", "expected etagnew")
tr.StillRunningAfter = ts

# 3 Test - Request fully healed asset via slice plugin
tr = Test.AddTestRun("Request full healed slice")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/second'
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression("bbbbb", "expected bbbbb content")
ps.Streams.stdout.Content = Testers.ContainsExpression("etagnew", "expected etagnew")
tr.StillRunningAfter = ts

# Test case: reference slice out of date (abort connection, heal reference)

req_header_refold0 = {"headers":
                      "GET /reference HTTP/1.1\r\n" +
                      "Host: www.example.com\r\n" +
                      "uuid: etagold-0\r\n" +
                      "Range: bytes=0-2\r\n"
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

res_header_refold0 = {"headers":
                      "HTTP/1.1 206 Partial Content\r\n" +
                      "Accept-Ranges: bytes\r\n" +
                      "Cache-Control: max-age=5000\r\n" +
                      "Connection: close\r\n" +
                      "Content-Range: bytes 0-2/5\r\n" +
                      'Etag: "etagold"\r\n' +
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "aaa"
                      }

server.addResponse("sessionlog.json", req_header_refold0, res_header_refold0)

req_header_refnew0 = {"headers":
                      "GET /reference HTTP/1.1\r\n" +
                      "Host: www.example.com\r\n" +
                      "uuid: etagnew-0\r\n" +
                      "Range: bytes=0-2\r\n"
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

res_header_refnew0 = {"headers":
                      "HTTP/1.1 206 Partial Content\r\n" +
                      "Accept-Ranges: bytes\r\n" +
                      "Cache-Control: max-age=5000\r\n" +
                      "Connection: close\r\n" +
                      "Content-Range: bytes 0-2/5\r\n" +
                      'Etag: "etagnew"\r\n' +
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "bbb"
                      }

server.addResponse("sessionlog.json", req_header_refnew0, res_header_refnew0)

req_header_refnew1 = {"headers":
                      "GET /reference HTTP/1.1\r\n" +
                      "Host: www.example.com\r\n" +
                      "uuid: etagnew-1\r\n" +
                      "Range: bytes=3-5\r\n"
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "",
                      }

res_header_refnew1 = {"headers":
                      "HTTP/1.1 206 Partial Content\r\n" +
                      "Accept-Ranges: bytes\r\n" +
                      "Cache-Control: max-age=5000\r\n" +
                      "Connection: close\r\n" +
                      "Content-Range: bytes 3-4/5\r\n" +
                      'Etag: "etagnew"\r\n' +
                      "\r\n",
                      "timestamp": "1469733493.993",
                      "body": "bb"
                      }

server.addResponse("sessionlog.json", req_header_refnew1, res_header_refnew1)

# 4 Test - Preload reference etagold-0
tr = Test.AddTestRun("Preload reference etagold-0")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://crr/reference -r 0-2 -H "uuid: etagold-0"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/aaa.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etagold", "expected etagold")
tr.StillRunningAfter = ts

# 5 Test - Preload reference etagnew-1
tr = Test.AddTestRun("Preload slice etagnew-1")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://crr/reference -r 3-5 -H "uuid: etagnew-1"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/bb.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etagnew", "expected etagnew")
tr.StillRunningAfter = ts

# 6 Test - Request reference slice via slice plugin, with instructions to
# fetch new 2nd slice -- this will send the old header, but abort and
# refetch it
tr = Test.AddTestRun("Request 2nd slice (expect abort)")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/reference -r 3- -H "uuid: etagnew-0" -w "SENT: \'%{size_download}\'"'
# ps.ReturnCode = 0 # curl will fail here
ps.Streams.stdout.Content = Testers.ContainsExpression("etagold", "expected etagold")
ps.Streams.stdout.Content += Testers.ContainsExpression("SENT: '0'", "expected empty payload")
tr.StillRunningAfter = ts

# 7 Test - Request full healed asset via slice plugin
tr = Test.AddTestRun("Request full healed slice")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/reference'
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression("bbbbb", "expected bbbbb content")
ps.Streams.stdout.Content = Testers.ContainsExpression("etagnew", "expected etagnew")
tr.StillRunningAfter = ts

# Request results in 200, not 206 (server not support range requests)

req_header_200 = {"headers":
                  "GET /code200 HTTP/1.1\r\n" +
                  "Host: www.example.com\r\n" +
                  "uuid: code200\r\n" +
                  "Range: bytes=3-5\r\n"
                  "\r\n",
                  "timestamp": "1469733493.993",
                  "body": "",
                  }

res_header_200 = {"headers":
                  "HTTP/1.1 200 OK\r\n" +
                  "Cache-Control: max-age=5000\r\n" +
                  "Connection: close\r\n" +
                  'Etag: "etag"\r\n' +
                  "\r\n",
                  "timestamp": "1469733493.993",
                  "body": "ccccc"
                  }

server.addResponse("sessionlog.json", req_header_200, res_header_200)

# 8 test - Request through slice but get a 200 back
tr = Test.AddTestRun("Request gets a 200")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/code200 -r 3-5 -H "uuid: code200"'
ps.ReturnCode = 0
ps.Streams.stderr.Content = Testers.ContainsExpression("ccccc", "expected full ccccc content")
ps.Streams.stdout.Content = Testers.ContainsExpression("200 OK", "expected 200")
tr.StillRunningAfter = ts

# Test for asset gone

# Preload
req_header_assetgone0 = {"headers":
                         "GET /assetgone HTTP/1.1\r\n" +
                         "Host: www.example.com\r\n" +
                         "uuid: assetgone-0\r\n" +
                         "Range: bytes=0-2\r\n"
                         "\r\n",
                         "timestamp": "1469733493.993",
                         "body": "",
                         }

res_header_assetgone0 = {"headers":
                         "HTTP/1.1 206 Partial Content\r\n" +
                         "Accept-Ranges: bytes\r\n" +
                         "Cache-Control: max-age=5000\r\n" +
                         "Connection: close\r\n" +
                         "Content-Range: bytes 0-2/5\r\n" +
                         'Etag: "etag"\r\n' +
                         "\r\n",
                         "timestamp": "1469733493.993",
                         "body": "aaa"
                         }

server.addResponse("sessionlog.json", req_header_assetgone0, res_header_assetgone0)

# 9 test - Preload reference slice
tr = Test.AddTestRun("Preload reference assetgone-0")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/assetgone -r 0-2 -H "uuid: assetgone-0"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/aaa.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etag", "expected etag")
tr.StillRunningAfter = ts

# 10 test - Fetch full asset, 2nd slice should trigger 404 response
tr = Test.AddTestRun("Fetch full asset")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/assetgone'
# ps.ReturnCode = 0 # curl will return non zero
ps.Streams.stderr = "gold/aaa.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etag", "expected etag")
ps.Streams.stdout.Content += Testers.ContainsExpression("Content-Length: 5", "expected header of content-length 5")
tr.StillRunningAfter = ts

# 11 test - Fetch full asset again, full blown 404
tr = Test.AddTestRun("Fetch full asset, 404")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slice/assetgone'
# ps.ReturnCode = 0 # curl will return non zero
ps.Streams.stdout.Content = Testers.ContainsExpression("404 Not Found", "Expected 404")
tr.StillRunningAfter = ts

# custom headers

edt = datetime.datetime.fromtimestamp(time.time() + 100)
edate = to_httpdate(edt)

# 12 Test - Preload reference etagold-1
tr = Test.AddTestRun("Preload slice etagold-1")
ps = tr.Processes.Default
ps.Command = curl_and_args + f' http://crrhdr/second -r 3-5 -H "uuid: etagold-1" -H "crr-foo: {edate}"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/aa.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etagold", "expected etagold")
tr.StillRunningAfter = ts

# 13 Test - Request second slice via slice plugin, with instructions to fetch new 2nd slice
tr = Test.AddTestRun("Request 2nd slice (expect refetch)")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://slicehdr/second -r 3- -H "uuid: etagnew-1"'
ps.ReturnCode = 0
ps.Streams.stderr = "gold/bb.gold"
ps.Streams.stdout.Content = Testers.ContainsExpression("etagnew", "expected etagnew")
tr.StillRunningAfter = ts

# Over riding the built in ERROR check since we expect to see logSliceErrors
ts.Disk.diags_log.Content = Testers.ContainsExpression("logSliceError", "logSliceErrors generated")
