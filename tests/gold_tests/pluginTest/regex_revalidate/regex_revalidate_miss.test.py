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
from jsonrpc import Request

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
ts = Test.MakeATSProcess("ts")

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
ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache')
ts.Disk.plugin_config.AddLine('regex_revalidate.so -d -c regex_revalidate.conf -l revalidate.log -m reval')

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

# 0 Request, cache miss expected
tr = Test.AddTestRun("Cache miss path1")
ps = tr.Processes.Default
ps.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
ps.StartBefore(Test.Processes.ts)
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss response")
tr.StillRunningAfter = ts

# 1 Request, cache hit expected
tr = Test.AddTestRun("Cache hit fresh path1")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit fresh response")
tr.StillRunningAfter = ts

# 2 Reload, populated config
tr = Test.AddTestRun("Reload config add path1")
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule + ' MISS')
# keep this for debug
tr.Disk.File(regex_revalidate_conf_path + "_tr2", typename="ats:config").AddLine(path1_rule + ' MISS')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5
# Delay it so the reload can catch up the diff between config files timestamps.
tr.DelayStart = 1

# 3 Request, cache miss expected
tr = Test.AddTestRun("Revalidate MISS path1")
ps = tr.Processes.Default
tr.DelayStart = 7
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss response")
tr.StillRunningAfter = ts

# 4 Request, cache hit (path1)
tr = Test.AddTestRun("Cache hit fresh path1")
ps = tr.Processes.Default
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit fresh response")
tr.StillRunningAfter = ts

# 5 Reload, change MISS to STALE (resets rule)
tr = Test.AddTestRun("Reload config path1 STALE")
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule + ' STALE')
tr.Disk.File(regex_revalidate_conf_path + "_tr5", typename="ats:config").AddLine(path1_rule + ' STALE')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5
# Delay it so the reload can catch up the diff between config files timestamps.
tr.DelayStart = 1

# 6 Request, cache stale expected
tr = Test.AddTestRun("Cache stale path1")
ps = tr.Processes.Default
tr.DelayStart = 7
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-stale", "expected cache hit stale response")
tr.StillRunningAfter = ts

# 7 Reload, change STALE to MISS (resets rule)
tr = Test.AddTestRun("Reload config path1 MISS")
ps = tr.Processes.Default
tr.Disk.File(regex_revalidate_conf_path, typename="ats:config").AddLine(path1_rule + ' MISS')
tr.Disk.File(regex_revalidate_conf_path + "_tr7", typename="ats:config").AddLine(path1_rule + ' MISS')
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5
# Delay it so the reload can catch up the diff between config files timestamps.
tr.DelayStart = 1

# 8 Request, cache miss expected
tr = Test.AddTestRun("Cache mis path1")
ps = tr.Processes.Default
tr.DelayStart = 7
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: miss", "expected cache miss response")
tr.StillRunningAfter = ts

# 9 Request, cache hit expected
tr = Test.AddTestRun("Cache hit path1")
ps = tr.Processes.Default
tr.DelayStart = 5
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit response")
tr.StillRunningAfter = ts

# 10 Reload, no changes
tr = Test.AddTestRun("Reload no changes")
ps = tr.Processes.Default
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload())
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5
# Delay it so the reload can catch up the diff between config files timestamps.
tr.DelayStart = 1

# 11 Request again, cache hit expected
tr = Test.AddTestRun("Cache hit path1")
ps = tr.Processes.Default
tr.DelayStart = 7
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit response")
tr.StillRunningAfter = ts

# 12 Stage - Reload of same time updated rules file
tr = Test.AddTestRun("Reload same config")
ps = tr.Processes.Default
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
ps.Command = f'touch {regex_revalidate_conf_path} ; traffic_ctl config reload'
ps.Env = ts.Env
ps.ReturnCode = 0
ps.TimeOut = 5
tr.TimeOut = 5

# 13 Request, Cache hit expected
tr = Test.AddTestRun("Cache hit path1")
ps = tr.Processes.Default
tr.DelayStart = 5
ps.Command = curl_and_args + ' http://ats/path1'
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("X-Cache: hit-fresh", "expected cache hit response")
tr.StillRunningAfter = ts

# 14 Stats check
tr = Test.AddTestRun("Check stats")
tr.DelayStart = 5
tr.Processes.Default.Command = f"bash -c ./metrics_miss.sh"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
