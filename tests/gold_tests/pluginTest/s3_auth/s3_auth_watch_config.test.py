'''
Test s3_auth config change watch function
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

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_INOTIFY'),
)

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = "s3_auth: watch config"

# define the request header and the desired response header
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

# desired response form the origin server
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}

# add request/response
server.addResponse("sessionlog.log", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'FileChange|s3_auth',
})

ts.Setup.CopyAs('rules/v4.conf', Test.RunDirectory)
ts.Setup.CopyAs('rules/v4-modified.conf', Test.RunDirectory)
ts.Setup.CopyAs('rules/region_map.conf', Test.RunDirectory)

ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0} \
        @plugin=s3_auth.so \
            @pparam=--config @pparam={1}/v4.conf \
            @pparam=--v4-region-map @pparam={1}/region_map.conf \
            @pparam=--watch-config \
            '
    .format(server.Variables.Port, Test.RunDirectory)
)

# Commands to get the following response headers
# 1. make a request
# 2. modify the config
# 3. make another request
curlRequest = (
    'curl -s -v -H "Host: www.example.com" http://127.0.0.1:{0};'
    'sleep 1; cp {1}/v4-modified.conf {1}/v4.conf;'
    'sleep 1; curl -s -v -H "Host: www.example.com" http://127.0.0.1:{0};'
)

# Test Case
tr = Test.AddTestRun()
tr.Processes.Default.Command = curlRequest.format(ts.Variables.port, Test.RunDirectory)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = "gold/s3_auth_basic.gold"
tr.StillRunningAfter = server

ts.Streams.stderr = "gold/traffic_server.gold"
ts.ReturnCode = 0
