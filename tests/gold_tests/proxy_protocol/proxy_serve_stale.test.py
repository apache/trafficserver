'''
Test child proxy serving stale content when parents are exhausted
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
# Set up hierarchical caching processes
ts_child = Test.MakeATSProcess("ts_child")
# Parent ATS process is not created to mock parent being "down"
# but parent hostname is recognized in hostdb to match with child successfully
ts_parent_hostname = "localhost:82"
server = Test.MakeOriginServer("server")

Test.testName = "STALE"

# Request from client
request_header = {"headers":
                  "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                  "timestamp": "1469733493.993",
                  "body": ""}
# Expected response from the origin server
response_header = {"headers":
                   "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=5,public\r\n\r\n",
                   "timestamp": "1469733493.993",
                   "body": "CACHED"}

# Add request/response
server.addResponse("sessionlog.log", request_header, response_header)

# Config child proxy to route to parent proxy
ts_child.Disk.records_config.update({
    'proxy.config.http.parent_proxy.fail_threshold': 2,
    'proxy.config.http.parent_proxy.total_connect_attempts': 1,
    'proxy.config.http.cache.max_stale_age': 90,
    'proxy.config.http.parent_proxy.self_detect': 0,
})
ts_child.Disk.parent_config.AddLine(
    f'dest_domain=. parent="{ts_parent_hostname}" round_robin=consistent_hash go_direct=false'
)
ts_child.Disk.remap_config.AddLine(
    f'map http://localhost:{ts_child.Variables.port} http://localhost:{server.Variables.Port}'
)

stale_output = "HTTP/1.1 200 OK\nServer: ATS/10.0.0\nAccept-Ranges: bytes\nContent-Length: 6\nCache-Control: public, max-age=5\n\nCACHED"


# Testing scenarios
# 1. Child proxy serves stale with warning header when parent returns invalid response
# 2. Child proxy serves stale with warning header when parent failcount meets fail_threshold and parent is unavailable
# 3. Child proxy does not serve stale when object is past the max_stale_age expiration date
curl_request = (
    f'curl -X PUSH -d "{stale_output}" "http://localhost:{ts_child.Variables.port}";'
    f'sleep 10; curl -s -v http://localhost:{ts_child.Variables.port};'  # 1. serve stale with warning, failcount=1
    f'curl -s -v http://localhost:{ts_child.Variables.port};'  # 1. serve stale with warning, failcount=2
    f'curl -s -v http://localhost:{ts_child.Variables.port};'  # 2. serve stale with warning, parent unavailable
    f'sleep 90; curl -v http://localhost:{ts_child.Variables.port}'  # 3. max_stale_age expires, stale content cannot be served
)

# Test case for when parent server is down but child proxy can serve cache object
tr = Test.AddTestRun()
tr.Processes.Default.Command = curl_request
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts_child)
tr.Processes.Default.Streams.stderr = "gold/proxy_serve_stale.gold"
tr.StillRunningAfter = ts_child
