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


def setPid(event):
    Test.Env['SERVERPID'] = str(event.object.pid())


Test.ContinueOnFail = True
# Set up hierarchical caching processes
ts_child = Test.MakeATSProcess("ts_child")
ts_parent = Test.MakeATSProcess("ts_parent")
server = Test.MakeOriginServer("server")
ts_parent.RunningEvent.Connect(setPid)

Test.testName = "STALE"

# Request from client
request_header = {"headers":
                  "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                  "timestamp": "1469733493.993",
                  "body": ""}
# Expected response from the origin server
response_header = {"headers":
                   "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: max-age=10,public\r\n\r\n",
                   "timestamp": "1469733493.993",
                   "body": "CACHED"}

# add request/response
server.addResponse("sessionlog.log", request_header, response_header)

# Config child proxy to route to parent proxy
ts_child.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|remap|parent|host',
    'proxy.config.http.cache.max_stale_age': 20,
})
ts_child.Disk.parent_config.AddLine(
    'dest_domain=. parent="localhost:{0}" round_robin=consistent_hash go_direct=false'.format(ts_parent.Variables.port)
)
ts_child.Disk.remap_config.AddLine(
    'map http://localhost:{0} http://localhost:{1}'.format(ts_child.Variables.port, server.Variables.Port)
)

# Config parent proxy to route to server
ts_parent.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|remap|parent',
    'proxy.config.http.cache.max_stale_age': 20,
})
ts_parent.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts_parent.Disk.remap_config.AddLine(
    'map http://localhost:{0} http://localhost:{0}'.format(server.Variables.Port)
)

# Request content to cache in parent proxy
curl_request_running = (
    'curl -s -v http://localhost:{0};'  # miss
    'sleep 5;curl -v http://localhost:{0}'  # hit-fresh
)

# Request stale content
curl_request_down = (
    'sleep 10; curl -s -v http://localhost:{0};'  # hit-stale, serve stale with warning
    'sleep 10; curl -v http://localhost:{0}'  # max_stale_age expires, can't reach parent
)

# Test case for when parent server is running, so child proxy can cache object
tr = Test.AddTestRun("Send request with parent running")
tr.Processes.Default.Command = curl_request_running.format(ts_child.Variables.port, ts_parent.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts_parent)
tr.Processes.Default.StartBefore(ts_child)
tr.Processes.Default.Streams.stderr = "gold/proxy_stale_parent_running.gold"
tr.StillRunningAfter = ts_child
tr.StillRunningAfter = ts_parent

tr = Test.AddTestRun("Kill parent server")
tr.Processes.Default.Command = 'kill ${SERVERPID}'
tr.Processes.Default.ReturnCode = 0

# Test case for when parent server is down, so child can serve stale content
tr = Test.AddTestRun("Send request with parent down")
tr.Processes.Default.Command = curl_request_down.format(ts_child.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/proxy_stale_parent_down.gold"
tr.StillRunningAfter = ts_child
