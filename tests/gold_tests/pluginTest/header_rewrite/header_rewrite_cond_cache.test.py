'''
Test cache lookup results in CACHE condition
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
Test.ContinueOnFail = True
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = "CACHE"

# Request from client
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
# Expected response from the origin server
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=10,public\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "CACHED"
}

# add request/response
server.addResponse("sessionlog.log", request_header, response_header)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
})

# This rule adds the cache header with results of the cache lookup results
# (ie. "hit-fresh", "hit-stale", "miss", "none")
ts.Setup.CopyAs('rules/rule_add_cache_result_header.conf', Test.RunDirectory)

ts.Disk.plugin_config.AddLine('header_rewrite.so {0}/rule_add_cache_result_header.conf'.format(Test.RunDirectory))
ts.Disk.remap_config.AddLine('map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port))
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(server.Variables.Port))

# Commands to get the following response headers
# 1. miss (empty cache)
# 2. hit-fresh (content served within 10s cache)
# 3. hit-stale (waited 15s, after 10s cache)
# 2. hit-fresh (content served within 10s cache)
curlRequest = (
    'curl -s -v -H "Host: www.example.com" http://127.0.0.1:{0};'
    'curl -v -H "Host: www.example.com" http://127.0.0.1:{0};'
    'sleep 15; curl -s -v -H "Host: www.example.com" http://127.0.0.1:{0};'
    'curl -s -v -H "Host: www.example.com" http://127.0.0.1:{0}')

# Test Case
tr = Test.AddTestRun()
tr.Processes.Default.Command = curlRequest.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = "gold/header_rewrite_cond_cache.gold"
tr.StillRunningAfter = server
