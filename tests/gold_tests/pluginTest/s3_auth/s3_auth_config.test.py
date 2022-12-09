'''
Test s3_auth config parsing
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

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

Test.testName = "s3_auth: config parsing"

# define the request header and the desired response header
request_header = {
    "headers": "GET /s3-bucket HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "x-amz-security-token": "hkMsi6/bfHyBKrSeM/H0hoXeyx8z1yZ/mJ0c+B/TqYx=tTJDjnQWtul38Z9iVJjeH1HB4VT2c=2o3yE3o=I9kmFs/lJDR85qWjB8e5asY/WbjyRpbAzmDipQpboIcYnUYg55bxrQFidV/q8gZa5A9MpR3n=op1C0lWjeBqcEJxpevNZxteSQTQfeGsi98Cdf+On=/SINVlKrNhMnmMsDOLMGx1YYt9d4UsRg1jtVrwxL4Vd/F7aHCZySAXKv+1rkhACR023wpa3dhp+xirGJxSO9LWwvcrTdM4xJo4RS8B40tGENOJ1NKixUJxwN/6og58Oft/u==uleR89Ja=7zszK2H7tX3DqmEYNvNDYQh/7VBRe5otghQtPwJzWpXAGk+Vme4hPPM5K6axH2LxipXzRiIV=oxNs0upKNu1FvuzbCQmkQdKQVmXl0344vngngrgN7wkEfrYtmKwICmpAS0cbW9jdSClgziVo4NaFc/hsIfok=4UA3hVtxIdw74lFNXD0RR7HKXkFPLIn85M7peOZsqMUCfO4gxr7KCfabszQQf0YcP/mt79XK50=WrSJG7oUyn+clUySPhlegqHAfT9a50uSK5WiQmOnGNGLF4wDO10sqKN1xRgQbYHPtwL+Ye0EMisvmYA3==kScorTSGaQWyibSWXAvxq9+IVGBYShVJ6S7DmTT=u/2d/fGEge+Xmbxlftza=cxJ=Md=k1Q71Lp6Boa56d7wtYRpK6tXHJ9I/2r7rN1E4OtwkFqb7SfWV3UXwyUrXyaaNPTIbqnAHnbgUGtuU6pgICpfREiIxVqvKBf6ErbxHRmMmAuYKxk5E9Mn6nnbxR4WTniweKYeDv2w39zge/tss+36Moeuio9d2eoyRFqXhq=rUGtDwX3fzXV0wV+dUojxOYQ57GQDl7+68PwHPcX794OIXuGOxBk83lNIYIcYz3Vc7qnGy6tFTz7f6S9+EZuSGN7TY5VKkT2eWye46DebrDF9Nwzs/FVpTzbPD/KGDIBtFIbazglhKoWe9txqb1QW8vFNNVOEhYa+cViO3g8ZmY1wG960US2zsnX5Eg8Q5a4h3+sxaJSJ4ONiXZWJuAgKRQzcrszu+M5C0ZVoCOv1goEgfNJeSm/yFc/3rx8wmeWLIJFtq65B7zF72HRKq1nthHAguaxXr20nguHpKkDpNBDVa=WwuJsbeGI",
    "body": ""
}

# desired response form the origin server
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "success!"
}

# add request/response
server.addResponse("sessionlog.log", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 's3_auth',
})

ts.Setup.CopyAs('rules/v4-parse-test.test_input', Test.RunDirectory)

ts.Disk.remap_config.AddLine(
    f'map http://www.example.com http://127.0.0.1:{server.Variables.Port} \
        @plugin=s3_auth.so \
            @pparam=--config @pparam={Test.RunDirectory}/v4-parse-test.test_input'
)

# Test Case
tr = Test.AddTestRun()
tr.Processes.Default.Command = f'curl -s -v -H "Host: www.example.com" http://127.0.0.1:{ts.Variables.port}/s3-bucket;'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = "gold/s3_auth_parsing.gold"
tr.StillRunningAfter = server

ts.Disk.traffic_out.Content = "gold/s3_auth_parsing_ts.gold"
ts.ReturnCode = 0
