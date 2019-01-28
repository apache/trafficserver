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
Test a regex_revalidate via remap plugin
'''

## Test description:
# Load up cache, ensure fresh
# Create regex reval rule, config reload:
#  ensure item is staled only once.
# Add a new rule, config reload:
#  ensure item isn't restaled again, but rule still in effect.

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work"),
    Condition.PluginExists('regex_revalidate.so'),
    Condition.PluginExists('xdebug.so')
)
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_manager")

#**testname is required**
#testName = "regex_reval"

# default root
request_header_v0 = { "headers":
    "GET / HTTP/1.1\r\n" +
    "Host: www.example.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}
response_header_v0 = { "headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    "Cache-Control: max-age=300\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "http://www.example.com/",
}
server.addResponse("sessionlog.json", request_header_v0, response_header_v0)

request_header_v1 = { "headers":
    "GET / HTTP/1.1\r\n" +
    "Host: www.reval.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}
response_header_v1 = { "headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    "Cache-Control: max-age=200\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "http://www.reval.com/",
}
server.addResponse("sessionlog.json", request_header_v1, response_header_v1)

# cache item alwaysfresh
request_header_0 = { "headers":
    "GET /alwaysfresh HTTP/1.1\r\n" +
    "Host: www.example.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_0 = { "headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    "Cache-Control: max-age=900,public\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "http://www.example.com/alwaysfresh"
}
server.addResponse("sessionlog.json", request_header_0, response_header_0)

# cache item path1
request_header_1 = {"headers":
    "GET /path1 HTTP/1.1\r\n" +
    "Host: www.reval.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_1 = {"headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    "Cache-Control: max-age=600,public\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "http://www.reval.com/path1"
}
server.addResponse("sessionlog.json", request_header_1, response_header_1)

# cache item path1a
request_header_1a = {"headers":
    "GET /path1a HTTP/1.1\r\n" +
    "Host: www.reval.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_1a = {"headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    "Cache-Control: max-age=600,public\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "http://www.reval.com/path1a"
}
server.addResponse("sessionlog.json", request_header_1a, response_header_1a)

# cache item path2
request_header_2 = {"headers":
    "GET /path2 HTTP/1.1\r\n" +
    "Host: www.reval.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_2 = {"headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    "Cache-Control: max-age=600,public\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "http://www.reval.com/path2"
}
server.addResponse("sessionlog.json", request_header_2, response_header_2)

# cache item path2a
request_header_3 = {"headers":
    "GET /path2a HTTP/1.1\r\n" +
    "Host: www.reval.com\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_3 = {"headers":
    "HTTP/1.1 200 OK\r\n" +
    "Connection: close\r\n" +
    "Cache-Control: max-age=900,public\r\n" +
    "\r\n",
    "timestamp": "1469733493.993",
    "body": "http://www.reval.com/path2a"
}
server.addResponse("sessionlog.json", request_header_3, response_header_3)

# Configure ATS server
ts.Disk.plugin_config.AddLine('xdebug.so')

reval_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'reval.conf')
remap_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'remap.config')

curlex_and_args = 'curl -s -D - -v -H "x-debug: x-cache" -H "Host: www.example.com"'
curlre_and_args = 'curl -s -D - -v -H "x-debug: x-cache" -H "Host: www.reval.com"'

# Define first revistion for when trafficserver starts
#ts.Disk.File(reval_conf_path, typename="ats:config").AddLines([
#    "# Empty\n"    
#])

ts.Disk.remap_config.AddLines([
    'map http://www.example.com/ http://127.0.0.1:{}/'.format(server.Variables.Port),
    'map http://www.reval.com/ http://127.0.0.1:{}/'.format(server.Variables.Port)
])

# minimal configuration
ts.Disk.records_config.update({
#    'proxy.config.diags.debug.enabled': 1,
#    'proxy.config.diags.debug.tags': 'regex_revalidate',
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.http.cache.http': 1,
    'proxy.config.http.wait_for_cache': 1,
    'proxy.config.http.insert_age_in_response': 0,
    'proxy.config.http.response_via_str': 3,
    'proxy.config.http.server_ports': '{}'.format(ts.Variables.port),
})

# 0 - load baseline
tr = Test.AddTestRun("Cache load alwaysfresh")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=1)
tr.DelayStart = 5
tr.Processes.Default.Command = curlex_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, "alwaysfresh")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-miss.gold"
tr.StillRunningAfter = ts

cache_paths = [ "path1", "path1a", "path2a" ]

# 1,2,3 - load
for path in cache_paths:
    tr = Test.AddTestRun("Cache load " + path)
    tr.Processes.Default.Command = curlre_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, path)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = "gold/regex_reval-miss.gold"
    tr.StillRunningAfter = ts

# 4 Stage - Load regex_revalidate plugin as remap
tr = Test.AddTestRun("Reload config add path1")
tr.Disk.File(reval_conf_path, typename="ats:config").AddLine(
    'path1 {}\n'.format(int(time.time()) + 600)
)
tr.Disk.File(remap_conf_path, typename="ats:config").AddLines([
    'map http://www.reval.com/ http://127.0.0.1:{}/ @plugin=regex_revalidate.so @pparam=-c @pparam=reval.conf'.format(server.Variables.Port),
    'map http://www.example.com/ http://127.0.0.1:{}/'.format(server.Variables.Port)
])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
#tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Remap plugin registration succeeded", 'plugin registration should exist')
tr.TimeOut = 5

# 5 Test - Cache hit (alwdaysfresh)
tr = Test.AddTestRun("Cache hit fresh alwaysfresh")
tr.DelayStart = 10
tr.Processes.Default.Command = curlex_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, "alwaysfresh")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts

# 6 Test - Revalidate path1
tr = Test.AddTestRun("Revalidate stale path1")
tr.Processes.Default.Command = curlre_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, "path1")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-stale.gold"
tr.StillRunningAfter = ts

# 7 Test - Cache hit (path1)
tr = Test.AddTestRun("Cache hit fresh path1")
tr.Processes.Default.Command = curlre_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, "path1")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts

# 8 Stage - Modify regex revalidate rules (check expiry carry forward)
tr = Test.AddTestRun("Reload config add path1")
tr.Disk.File(reval_conf_path, typename="ats:config").AddLines([
    'path1 {}\n'.format(int(time.time()) + 900),
    'justanotherule {}\n'.format(int(time.time()) + 200)
])
tr.Disk.File(remap_conf_path, typename="ats:config").AddLines([
    'map http://www.reval.com/ http://127.0.0.1:{}/ @plugin=regex_revalidate.so @pparam=-c @pparam=reval.conf'.format(server.Variables.Port),
    'map http://www.example.com/ http://127.0.0.1:{}/'.format(server.Variables.Port)
])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5
# look for "Deleting remap plugin instance" before continuing
#tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Remap plugin registration succeeded", 'should exist')
#tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("Deleting remap plugin instance", 'should exist')

# 9 Test - Cache hit (alwdaysfresh)
tr = Test.AddTestRun("Cache hit fresh alwaysfresh")
tr.DelayStart = 10
tr.Processes.Default.Command = curlex_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, "alwaysfresh")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts

# 10 Test - Cache hit (path1)
tr = Test.AddTestRun("Cache hit fresh path1")
tr.DelayStart = 10
tr.Processes.Default.Command = curlre_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, "path1")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts

# 11 Stage - Unload regex revalidate rules
tr = Test.AddTestRun("Reload config remove revalidate")
tr.Disk.File(remap_conf_path, typename="ats:config").AddLines([
    'map http://www.reval.com/ http://127.0.0.1:{}/'.format(server.Variables.Port),
    'map http://www.example.com/ http://127.0.0.1:{}/'.format(server.Variables.Port)
])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
# look for "Deleting remap plugin instance" before continuing
#tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Deleting remap plugin instance", 'should exist')
tr.TimeOut = 5

# 12 Test - Cache hit (alwdaysfresh)
tr = Test.AddTestRun("Cache hit fresh alwaysfresh")
tr.DelayStart = 10
tr.Processes.Default.Command = curlex_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, "alwaysfresh")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts

# 13 Test - Cache hit (path1a, would have been stale with plugin)
tr = Test.AddTestRun("Cache hit fresh path1a")
tr.DelayStart = 10
tr.Processes.Default.Command = curlre_and_args + ' http://127.0.0.1:{}/{}'.format(ts.Variables.port, "path1a")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/regex_reval-hit.gold"
tr.StillRunningAfter = ts
