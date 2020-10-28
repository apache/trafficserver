'''
Test cached responses and requests with bodies
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
Test cached responses and requests with bodies
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

# **testname is required**
testName = ""
request_header1 = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header1 = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=300\r\n\r\n",
                    "timestamp": "1469733493.993", "body": "xxx"}
request_header2 = {"headers": "GET /no_cache_control HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
response_header2 = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                    "timestamp": "1469733493.993", "body": "the flinstones"}
request_header3 = {"headers": "GET /max_age_10sec HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
response_header3 = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=10,public\r\n\r\n",
                    "timestamp": "1469733493.993", "body": "yabadabadoo"}
server.addResponse("sessionlog.json", request_header1, response_header1)
server.addResponse("sessionlog.json", request_header2, response_header2)
server.addResponse("sessionlog.json", request_header3, response_header3)

# ATS Configuration
ts.Disk.plugin_config.AddLine('xdebug.so')
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.http.response_via_str': 3,
    'proxy.config.http.insert_age_in_response': 0,
})

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

# Test 1 - 200 response and cache fill
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'curl -s -D - -v --ipv4 --http1.1 -H "x-debug: x-cache,via" -H "Host: www.example.com" http://localhost:{port}/max_age_10sec'.format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_and_req_body-miss.gold"
tr.StillRunningAfter = ts

# Test 2 - 200 cached response and using netcat
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET /max_age_10sec HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_and_req_body-hit.gold"
tr.StillRunningAfter = ts

# Test 3 - response with no cache control, so cache-miss every time
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET /no_cache_control HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_no_cc.gold"
tr.StillRunningAfter = ts

# Test 4 - Cache-Control: no-cache (from client), so cache miss every time
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET /no_cache_control HTTP/1.1\r\n''Cache-Control:no-cache\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_no_cc.gold"
tr.StillRunningAfter = ts

# Test 5 - hit stale cache.
tr = Test.AddTestRun()
tr.Processes.Default.Command = "sleep 15; printf 'GET /max_age_10sec HTTP/1.1\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_hit_stale.gold"
tr.StillRunningAfter = ts

# Test 6 - only-if-cached. 504 "Not Cached" should be returned if not in cache
tr = Test.AddTestRun()
tr.Processes.Default.Command = "printf 'GET /no_cache_control HTTP/1.1\r\n''Cache-Control: only-if-cached\r\n''x-debug: x-cache,x-cache-key,via\r\n''Host: www.example.com\r\n''Cache-control: max-age=300\r\n''\r\n'|nc 127.0.0.1 -w 1 {port}".format(
    port=ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/cache_no_cache.gold"
tr.StillRunningAfter = ts

#
# Verify correct handling of various max-age directives in both clients and
# responses.
#
ts = Test.MakeATSProcess("ts-for-proxy-verifier")
replay_file = "replay/cache-control-max-age.replay.yaml"
server = Test.MakeVerifierServerProcess("proxy-verifier-server", replay_file)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http',
    'proxy.config.http.insert_age_in_response': 0,

    # Disable ignoring max-age in the client request so we can test that
    # behavior too.
    'proxy.config.http.cache.ignore_client_cc_max_age': 0,
})
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.http_port)
)
tr = Test.AddTestRun("Verify correct max-age cache-control behavior.")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("proxy-verifier-client", replay_file, http_ports=[ts.Variables.port])
