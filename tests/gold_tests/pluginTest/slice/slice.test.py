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

## Test description:
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
ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=True)

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

request_header = {"headers":
  "GET /path HTTP/1.1\r\n" +
  "Host: www.example.com\r\n" +
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

ts.Setup.CopyAs('curlsort.sh', Test.RunDirectory)
curl_and_args = 'sh curlsort.sh -H "Host: www.example.com"'

# set up whole asset fetch into cache
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{}'.format(server.Variables.Port)
)

# minimal configuration
ts.Disk.records_config.update({
  'proxy.config.diags.debug.enabled': 1,
  'proxy.config.diags.debug.tags': 'slice',
  'proxy.config.http.cache.http': 1,
  'proxy.config.http.wait_for_cache': 1,
  'proxy.config.http.insert_age_in_response': 0,
  'proxy.config.http.response_via_str': 3,
})

# 0 Test - Prefetch entire asset into cache
tr = Test.AddTestRun("Fetch first slice range")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/slice_200.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold/slice_200.stderr.gold"
tr.StillRunningAfter = ts

block_bytes = 7

# 1 - Reconfigure remap.config with slice plugin
tr = Test.AddTestRun("Load Slice plugin")
remap_config_path = ts.Disk.remap_config.Name
tr.Disk.File(remap_config_path, typename="ats:config").AddLines([
  'map / http://127.0.0.1:{}'.format(server.Variables.Port) +
    ' @plugin=slice.so @pparam=--blockbytes-test={}'.format(block_bytes)
])

tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5

# 2 Test - First complete slice
tr = Test.AddTestRun("Fetch first slice range")
tr.DelayStart = 5
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path'.format(ts.Variables.port) + ' -r 0-6'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/slice_first.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold/slice_first.stderr.gold"
tr.StillRunningAfter = ts

# 3 Test - Last slice auto
tr = Test.AddTestRun("Last slice -- 14-")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path'.format(ts.Variables.port) + ' -r 14-'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/slice_last.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold/slice_last.stderr.gold"
tr.StillRunningAfter = ts

# 4 Test - Last slice exact
tr = Test.AddTestRun("Last slice 14-17")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path'.format(ts.Variables.port) + ' -r 14-17'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/slice_last.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold/slice_last.stderr.gold"
tr.StillRunningAfter = ts

# 5 Test - Last slice truncated
tr = Test.AddTestRun("Last truncated slice 14-20")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path'.format(ts.Variables.port) + ' -r 14-20'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/slice_last.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold/slice_last.stderr.gold"
tr.StillRunningAfter = ts

# 6 Test - Whole asset via slices
tr = Test.AddTestRun("Whole asset via slices")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/slice_200.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold/slice_200.stderr.gold"
tr.StillRunningAfter = ts

# 7 Test - Whole asset via range
tr = Test.AddTestRun("Whole asset via range")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path'.format(ts.Variables.port) + ' -r 0-'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/slice_206.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold/slice_206.stderr.gold"
tr.StillRunningAfter = ts

# 8 Test - Non aligned slice request
tr = Test.AddTestRun("Non aligned slice request")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path'.format(ts.Variables.port) + ' -r 5-16'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/slice_mid.stdout.gold"
tr.Processes.Default.Streams.stderr = "gold/slice_mid.stderr.gold"
tr.StillRunningAfter = ts
