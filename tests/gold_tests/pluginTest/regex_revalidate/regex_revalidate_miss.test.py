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
regex_revalidate plugin test, MISS (refetch) functionality
'''

# Test description:
# If MISS tag encountered, should load rule as refetch instead of IMS.
# If rule switched from MISS to IMS or vice versa, rule should reset.

Test.SkipUnless(Condition.PluginExists('regex_revalidate.so'), Condition.PluginExists('xdebug.so'))
Test.ContinueOnFail = False

# configure origin server
server = Test.MakeOriginServer("server")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", command="traffic_manager")

Test.testName = "regex_revalidate_miss"
Test.Setup.Copy("metrics_miss.sh")

# default root
request_header_0 = {
    "headers": "GET / HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "",
}

response_header_0 = {
    "headers": "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + "Cache-Control: max-age=300\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "xxx",
}

# cache item path1
request_header_1 = {
    "headers": "GET /path1 HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header_1 = {
    "headers":
        "HTTP/1.1 200 OK\r\n" + "Connection: close\r\n" + 'Etag: "path1"\r\n' + "Cache-Control: max-age=600,public\r\n" + "\r\n",
    "timestamp": "1469733493.993",
    "body": "abc"
}

server.addResponse("sessionlog.json", request_header_0, response_header_0)
server.addResponse("sessionlog.json", request_header_1, response_header_1)

# Configure ATS server
ts.Disk.plugin_config.AddLine('xdebug.so')
ts.Disk.plugin_config.AddLine('regex_revalidate.so -d -c regex_revalidate.conf -l revalidate.log')

regex_revalidate_conf_path = os.path.join(ts.Variables.CONFIGDIR, 'regex_revalidate.conf')
#curl_and_args = 'curl -s -D - -v -H "x-debug: x-cache" -H "Host: www.example.com"'

path1_rule = 'path1 {}'.format(int(time.time()) + 600)

# Define first revision for when trafficserver starts
ts.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine("# Empty")

ts.Disk.remap_config.AddLine('map http://ats/ http://127.0.0.1:{}'.format(server.Variables.Port))

# minimal configuration
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'regex_revalidate',
        'proxy.config.http.insert_age_in_response': 0,
        'proxy.config.http.response_via_str': 3,
        'proxy.config.http.cache.http': 1,
        'proxy.config.http.wait_for_cache': 1,
    })

curl_and_args = 'curl -s -D /dev/stdout -o /dev/stderr -x http://127.0.0.1:{}'.format(ts.Variables.port) + ' -H "x-debug: x-cache"'

# 0 Test - Load cache (miss) (path1)
tr = Test.AddTestRun("Cache miss path1")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss response")
tr.StillRunningAfter = ts

# 1 Test - Cache hit path1
tr = Test.AddTestRun("Cache hit fresh path1")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit fresh response")
tr.StillRunningAfter = ts

# 2 Stage - Load new regex_revalidate
tr = Test.AddTestRun("Reload config add path1")
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule + ' MISS')
# keep this for debug
tr.Disk.File(regex_revalidate_conf_path + "_tr2", typename="ats:config").AddLine(path1_rule + ' MISS')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
ps.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 3 Test - Revalidate path1
tr = Test.AddTestRun("Revalidate MISS path1")
ps = tr.Processes.Default
tr.DelayStart = 5
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss response")
tr.StillRunningAfter = ts

# 4 Test - Cache hit (path1)
tr = Test.AddTestRun("Cache hit fresh path1")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit fresh response")
tr.StillRunningAfter = ts

# 5 Stage - Change from MISS to STALE, reload
tr = Test.AddTestRun("Reload config path1 STALE")
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule + ' STALE')
tr.Disk.File(regex_revalidate_conf_path + "_tr5", typename="ats:config").AddLine(path1_rule + ' STALE')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
ps.Command = 'traffic_ctl config reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 6 Test - Cache stale
tr = Test.AddTestRun("Cache stale path1")
ps = tr.Processes.Default
tr.DelayStart = 5
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit stale response")
tr.StillRunningAfter = ts

# 7 Stage - Switch back to MISS
tr = Test.AddTestRun("Reload config path1 MISS")
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule + ' MISS')
tr.Disk.File(regex_revalidate_conf_path + "_tr7", typename="ats:config").AddLine(path1_rule + ' MISS')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
ps.Command = 'traffic_ctl config reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 8 Test - Cache stale
tr = Test.AddTestRun("Cache stale path1")
ps = tr.Processes.Default
tr.DelayStart = 5
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss response")
tr.StillRunningAfter = ts

# 9 Stage - Write out same contents, ensure rule not reset
tr = Test.AddTestRun("Reload config path1 MISS again")
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule + ' MISS')
tr.Disk.File(regex_revalidate_conf_path + "_tr9", typename="ats:config").AddLine(path1_rule + ' MISSSTALE')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
ps.Command = 'traffic_ctl config reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 10 Test - Cache stale
tr = Test.AddTestRun("Cache stale path1")
ps = tr.Processes.Default
tr.DelayStart = 5
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit response")
tr.StillRunningAfter = ts

# 11 Stats check
tr = Test.AddTestRun("Check stats")
tr.DelayStart = 5
tr.Processes.Default.Command = "bash -c ./metrics_miss.sh"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
