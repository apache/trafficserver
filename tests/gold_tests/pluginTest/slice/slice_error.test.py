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
import time
Test.Summary = '''
Slice plugin error.log test
'''

## Test description:
# Preload the cache with the entire asset to be range requested.
# Reload remap rule with slice plugin
# Request content through the slice plugin

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl needs to be installed on system for this test to work"),
    Condition.PluginExists('slice.so'),
)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server", lookup_key="{%Range}{PATH}")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_manager")

body = "the quick brown fox" # len 19

# default root
request_header_chk = {"headers":
  "GET / HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
  "Range: bytes=0-\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": "",
}

response_header_chk = {"headers":
  "HTTP/1.1 206 Partial Content\r\n" +
  "Connection: close\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": body,
}

server.addResponse("sessionlog.json", request_header_chk, response_header_chk)

blockbytes = 9

range0 = "{}-{}".format(0, blockbytes - 1)
range1 = "{}-{}".format(blockbytes, (2 * blockbytes) - 1)

body0 = body[0:blockbytes]
body1 = body[blockbytes:2 * blockbytes]

# Mismatch etag

request_header_etag0 = {"headers":
  "GET /etag HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
  "Range: bytes={}\r\n".format(range0) +
  "X-Slicer-Info: full content request\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": "",
}

response_header_etag0 = {"headers":
  "HTTP/1.1 206 Partial Content\r\n" +
  "Connection: close\r\n" +
  'Etag: "etag0"\r\n' +
  "Content-Range: bytes {}/{}\r\n".format(range0, len(body)) +
  "Cache-Control: max-age=500\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": body0,
}

server.addResponse("sessionlog.json", request_header_etag0, response_header_etag0)

request_header_etag1 = {"headers":
  "GET /etag HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
  "Range: bytes={}\r\n".format(range1) +
  "X-Slicer-Info: full content request\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": "",
}

response_header_etag1 = {"headers":
  "HTTP/1.1 206 Partial Content\r\n" +
  "Connection: close\r\n" +
  'Etag: "etag1"\r\n' +
  "Content-Range: bytes {}/{}\r\n".format(range1, len(body)) +
  "Cache-Control: max-age=500\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": body1,
}

server.addResponse("sessionlog.json", request_header_etag1, response_header_etag1)

# mismatch Last-Modified

request_header_lm0 = {"headers":
  "GET /lastmodified HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
  "Range: bytes={}\r\n".format(range0) +
  "X-Slicer-Info: full content request\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": "",
}

response_header_lm0 = {"headers":
  "HTTP/1.1 206 Partial Content\r\n" +
  "Connection: close\r\n" +
  "Last-Modified: Tue, 08 May 2018 15:49:41 GMT\r\n" +
  "Content-Range: bytes {}/{}\r\n".format(range0, len(body)) +
  "Cache-Control: max-age=500\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": body0,
}

server.addResponse("sessionlog.json", request_header_lm0, response_header_lm0)

request_header_lm1 = {"headers":
  "GET /lastmodified HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
  "Range: bytes={}\r\n".format(range1) +
  "X-Slicer-Info: full content request\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": "",
}

response_header_lm1 = {"headers":
  "HTTP/1.1 206 Partial Content\r\n" +
  "Connection: close\r\n" +
  "Last-Modified: Tue, 08 Apr 2019 18:00:00 GMT\r\n" +
  "Content-Range: bytes {}/{}\r\n".format(range1, len(body)) +
  "Cache-Control: max-age=500\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": body1,
}

server.addResponse("sessionlog.json", request_header_lm1, response_header_lm1)

# non 206 slice block

request_header_n206_0 = {"headers":
  "GET /non206 HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
  "Range: bytes={}\r\n".format(range0) +
  "X-Slicer-Info: full content request\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": "",
}

response_header_n206_0 = {"headers":
  "HTTP/1.1 206 Partial Content\r\n" +
  "Connection: close\r\n" +
  'Etag: "etag"\r\n' +
  "Last-Modified: Tue, 08 May 2018 15:49:41 GMT\r\n" +
  "Content-Range: bytes {}/{}\r\n".format(range0, len(body)) +
  "Cache-Control: max-age=500\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": body0,
}

server.addResponse("sessionlog.json", request_header_n206_0, response_header_n206_0)

# mismatch content-range

request_header_crr0 = {"headers":
  "GET /crr HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
  "Range: bytes={}\r\n".format(range0) +
  "X-Slicer-Info: full content request\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": "",
}

response_header_crr0 = {"headers":
  "HTTP/1.1 206 Partial Content\r\n" +
  "Connection: close\r\n" +
  "Etag: crr\r\n" +
  "Content-Range: bytes {}/{}\r\n".format(range0, len(body)) +
  "Cache-Control: max-age=500\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": body0,
}

server.addResponse("sessionlog.json", request_header_crr0, response_header_crr0)

request_header_crr1 = {"headers":
  "GET /crr HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
  "Range: bytes={}\r\n".format(range1) +
  "X-Slicer-Info: full content request\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": "",
}

response_header_crr1 = {"headers":
  "HTTP/1.1 206 Partial Content\r\n" +
  "Connection: close\r\n" +
  "Etag: crr\r\n" +
  "Content-Range: bytes {}/{}\r\n".format(range1, len(body) - 1) +
  "Cache-Control: max-age=500\r\n" +
  "\r\n",
  "timestamp": "1469733493.993",
  "body": body1,
}

server.addResponse("sessionlog.json", request_header_crr1, response_header_crr1)

ts.Setup.CopyAs('curlsort.sh', Test.RunDirectory)
curl_and_args = 'sh curlsort.sh -H "Host: www.example.com"'

# set up whole asset fetch into cache
ts.Disk.remap_config.AddLine(
  'map / http://127.0.0.1:{}'.format(server.Variables.Port) +
    ' @plugin=slice.so @pparam=--test-blockbytes={}'.format(blockbytes)
)

# minimal configuration
ts.Disk.records_config.update({
  'proxy.config.diags.debug.enabled': 0,
  'proxy.config.diags.debug.tags': 'slice',
  'proxy.config.http.cache.http': 0,
  'proxy.config.http.wait_for_cache': 0,
  'proxy.config.http.insert_age_in_response': 0,
  'proxy.config.http.insert_request_via_str': 0,
  'proxy.config.http.insert_response_via_str': 3,
  'proxy.config.http.server_ports': '{}'.format(ts.Variables.port),
})

# Override builtin error check as these cases will fail
# taken from the slice plug code
ts.Disk.diags_log.Content = Testers.ContainsExpression('reason="Mismatch block Etag"', "Mismatch block etag")
ts.Disk.diags_log.Content += Testers.ContainsExpression('reason="Mismatch block Last-Modified"', "Mismatch block Last-Modified")
ts.Disk.diags_log.Content += Testers.ContainsExpression('reason="Non 206 internal block response"', "Non 206 internal block response")
ts.Disk.diags_log.Content += Testers.ContainsExpression('reason="Mismatch/Bad block Content-Range"', "Mismatch/Bad block Content-Range")

# 0 Test - Etag mismatch test
tr = Test.AddTestRun("Etag test")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=1)
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/etag'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold_error/etag.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold_error/etag.stderr.gold"
tr.StillRunningAfter = ts

# 1 Check - diags.log message
tr = Test.AddTestRun("Etag error check")
tr.Processes.Default.Command = "grep 'Mismatch block Etag' {}".format(ts.Disk.diags_log.Name)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# 2 Test - Last Modified mismatch test
tr = Test.AddTestRun("Last-Modified test")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/lastmodified'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold_error/lm.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold_error/lm.stderr.gold"
tr.StillRunningAfter = ts

# 3 Check - diags.log message
tr = Test.AddTestRun("Last-Modified error check")
tr.Processes.Default.Command = "grep 'Mismatch block Last-Modified' {}".format(ts.Disk.diags_log.Name)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# 4 Test - Non 206 mismatch test
tr = Test.AddTestRun("Non 206 test")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/non206'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold_error/non206.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold_error/non206.stderr.gold"
tr.StillRunningAfter = ts

# 3 Check - diags.log message
tr = Test.AddTestRun("Non 206 error check")
tr.Processes.Default.Command = "grep 'Non 206 internal block response' {}".format(ts.Disk.diags_log.Name)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# 4 Test - Block content-range
tr = Test.AddTestRun("Content-Range test")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/crr'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold_error/crr.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold_error/crr.stderr.gold"
tr.StillRunningAfter = ts

# 3 Check - diags.log message
tr = Test.AddTestRun("Content-Range error check")
tr.Processes.Default.Command = "grep 'Mismatch/Bad block Content-Range' {}".format(ts.Disk.diags_log.Name)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
