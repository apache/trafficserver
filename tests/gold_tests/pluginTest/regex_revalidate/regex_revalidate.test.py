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
Test a basic regex_revalidate
'''

## Test description:
# Load up cache, ensure fresh
# Create regex reval rule, config reload:
#  ensure item is staled only once.
# Add a new rule, config reload:
#  ensure item isn't restaled again, but rule still in effect.
#
# If the rule disappears from regex_revalidate.conf its still loaded!!
# A rule's expiry can't be changed after the fact!

Test.SkipUnless(
    Condition.PluginExists('regex_revalidate.so'),
    Condition.PluginExists('xdebug.so')
)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=True)

#**testname is required**
#testName = "regex_reval"

# default root
request_header_0 = {"headers":
    "GET / HTTP/1.1\r\n" +
    "Host: www.example.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

response_header_0 = {"headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    "Cache-Control: max-age=300\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx",
}

# cache item path1
request_header_1 = {"headers":
    "GET /path1 HTTP/1.1\r\n" +
    "Host: www.example.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_1 = {"headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    'Etag: "path1"\r\n' +
    "Cache-Control: max-age=600,public\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "abc"
}

# cache item path1a
request_header_2 = {"headers":
    "GET /path1a HTTP/1.1\r\n" +
    "Host: www.example.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_2 = {"headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    'Etag: "path1a"\r\n' +
    "Cache-Control: max-age=600,public\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "cde"
}

# cache item path2a
request_header_3 = {"headers":
    "GET /path2a HTTP/1.1\r\n" +
    "Host: www.example.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_3 = {"headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    'Etag: "path2a"\r\n' +
    "Cache-Control: max-age=900,public\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "efg"
}

server.addResponse("sessionlog.json", request_header_0, response_header_0)
server.addResponse("sessionlog.json", request_header_1, response_header_1)
server.addResponse("sessionlog.json", request_header_2, response_header_2)
server.addResponse("sessionlog.json", request_header_3, response_header_3)

# Configure ATS server
ts.Disk.plugin_config.AddLine('xdebug.so')
ts.Disk.plugin_config.AddLine(
    'regex_revalidate.so -d -c regex_revalidate.conf'
)

regex_revalidate_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'regex_revalidate.conf')
curl_and_args = 'curl -s -D - -v -H "x-debug: x-cache" -H "Host: www.example.com"'

path1_rule = 'path1 {}\n'.format(int(time.time()) + 600)

# Define first revistion for when trafficserver starts
ts.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLines([
    "# Empty\n"
])

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{}'.format(server.Variables.Port)
)

# minimal configuration
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'regex_revalidate',
#    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.http.cache.http': 1,
    'proxy.config.http.wait_for_cache': 1,
    'proxy.config.http.insert_age_in_response': 0,
    'proxy.config.http.response_via_str': 3,
})

# 0 Test - Load cache (miss) (path1)
tr = Test.AddTestRun("Cache miss path1")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path1'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-miss.gold"
tr.StillRunningAfter = ts

# 1 Test - Load cache (miss) for later test (path1a)
tr = Test.AddTestRun("Cache miss path1a")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path1a'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-miss.gold"
tr.StillRunningAfter = ts

# 2 Test - Load cache (miss) for later test (path2a)
tr = Test.AddTestRun("Cache miss path2a")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path2a'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-miss.gold"
tr.StillRunningAfter = ts

# 3 Test - Cache hit path1
tr = Test.AddTestRun("Cache hit fresh path1")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path1'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts

# 4 Stage - Reload new regex_revalidate
tr = Test.AddTestRun("Reload config add path1")
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLines([
    path1_rule
])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5

# 5 Test - Revalidate path1
tr = Test.AddTestRun("Revalidate stale path1")
tr.DelayStart = 5
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path1'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-stale.gold"
tr.StillRunningAfter = ts

# 6 Test - Cache hit (path1)
tr = Test.AddTestRun("Cache hit fresh path1")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path1'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts

# 7 Stage - Reload new regex_revalidate
tr = Test.AddTestRun("Reload config add path2")
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLines([
    path1_rule,
    'path2 {}\n'.format(int(time.time()) + 700)
])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5

# 8 Test - Cache hit (path1)
tr = Test.AddTestRun("Cache hit fresh path1")
tr.DelayStart = 5
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path1'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts

# 9 Test - Cache stale (check rule is still loaded) (path1a)
tr = Test.AddTestRun("Revalidate stale path1a")
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path1a'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-stale.gold"
tr.StillRunningAfter = ts

# The C version of regex_revalidate doesn't allow an existing rule to
# be changed by a reload.

# 10 Stage - regex_revalidate rewrite rule early expire
tr = Test.AddTestRun("Reload config change path2")
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLines([
    path1_rule,
    'path2 {}\n'.format(int(time.time()) - 100),
])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5

# 11 Test - Cache hit (path2a)
tr = Test.AddTestRun("Cache hit stale path2a")
tr.DelayStart = 5
tr.Processes.Default.Command = curl_and_args + ' http://127.0.0.1:{}/path2a'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-stale.gold"
tr.StillRunningAfter = ts
