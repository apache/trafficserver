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

Test.Summary = '''
Test requests with bad HTTP formats
'''

Test.SkipUnless(Condition.HasProgram("nc", "Netcat needs to be installed on system for this test to work"))

ArbitraryTimestamp = '12345678'

random_path = "/sdfsdf"

random_method = "xyzxyz"

ts = Test.MakeATSProcess("ts")

Test.GetTcpPort("upstream_port")

method_server = Test.Processes.Process(
    "method-server", "bash -c '" + Test.TestDirectory + f"/method-server.sh {Test.Variables.upstream_port} outserver'")

server = Test.MakeOriginServer("server", ssl=False)
request_header = {
    "headers": "GET {}/0 HTTP/1.1\r\nX-Req-Id: 0\r\nHost: example.com\r\n\r\n".format(random_path),
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nX-Resp-Id: 1\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
server.addResponse("sessionlog.json", request_header, response_header)

request_header = {
    "headers": "GET {}/1 HTTP/1.1\r\nX-Req-Id: 1\r\nHost: example.com\r\n\r\n".format(random_path),
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nX-Resp-Id: 2\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
server.addResponse("sessionlog.json", request_header, response_header)

request_header = {
    "headers": "GET /example/1 HTTP/1.1\r\nX-Req-Id: 6\r\nHost: example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nX-Resp-Id: 3\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
server.addResponse("sessionlog.json", request_header, response_header)

request_header = {
    "headers": "GET /example/2 HTTP/1.1\r\nX-Req-Id: 7\r\nHost: example.com\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nX-Resp-Id: 4\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns',
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.http.strict_uri_parsing': 1,
        'proxy.config.http.cache.http': 0,
    })

# Allow random_method for 128.0.0.1, do not allow it for ::1.
#
ts.Disk.ip_allow_yaml.AddLines(
    [
        'ip_allow:',
        '  - apply: in',
        '    ip_addrs: 127.0.0.1',
        '    action: allow',
        '    methods:',
        '      - GET',
        f'      - {random_method}',
    ])
ts.Disk.ip_allow_yaml.AddLines([
    '  - apply: in',
    '    ip_addrs: ::1',
    '    action: allow',
    '    methods:',
    '      - GET',
])

ts.Disk.remap_config.AddLine('map /add-method http://127.0.0.1:{0}/'.format(Test.Variables.upstream_port))
ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}/'.format(server.Variables.Port))

# Even if the request from the client is HTTP version 1.0, ATS's request to server will be HTTP version 1.1.
#
tr = Test.AddTestRun("success-1.0")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.Command = (
    f"printf 'GET {random_path}/0HTTP/1.0\r\n" + "Host: example.com\r\n" + "Connection: close\r\n" + "X-Req-Id: 1\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("success-1.1")
tr.Processes.Default.Command = (
    f"printf 'GET {random_path}/1HTTP/1.1\r\n" + "Host: example.com\r\n" + "Connection: close\r\n" + "X-Req-Id: 2\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("invalid-url-line")
tr.Processes.Default.Command = (
    f"printf 'GET {random_path}<HTTP/1.1\r\n" + "Host: example.com\r\n" + "Connection: close\r\n" + "X-Req-Id: 2\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("bad-http-version")
tr.Processes.Default.Command = (
    f"printf 'GET {random_path} HTTP/1.2\r\n" + "Host: example.com\r\n" + "Connection: close\r\n" + "X-Req-Id: 3\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("bad-request-maybe-http-version")
tr.Processes.Default.Command = (
    f"printf 'GET {random_path} HTTP/0.9\r\n" + "Host: example.com\r\n" + "Connection: close\r\n" + "X-Req-Id: 4\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("same-as-last-case?")
tr.Processes.Default.Command = (
    fr"printf 'GET {random_path} HTTP/0.9\r\n" + "Host: example.com\r\n" + "Connection: close\r\n" + "X-Req-Id: 5\r\n\r\n'" +
    fr"| nc localhost {ts.Variables.port} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("allowed-random-method")
tr.Processes.Default.StartBefore(method_server)
tr.Processes.Default.Command = (
    fr"printf '{random_method} /add-method HTTP/1.1\r\n" + "Host: example.com\r\n" + "Connection: close\r\n" +
    "X-Req-Id: 6\r\n\r\n'" + f" | nc localhost {ts.Variables.port} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("valid-ipv6")
tr.Processes.Default.Command = (
    f"printf 'GET /example/2 HTTP/1.1\r\n" + "Host: example.com\r\n" + "Connection: close\r\n" + "X-Req-Id: 7\r\n\r\n'" +
    f" | nc ::1 {ts.Variables.portv6} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("unallowed-method-v6")
tr.Processes.Default.Command = (
    f"printf '{random_method} /example/1 HTTP/1.1\r\n" + "Host: example.com\r\n" + "Connection: close\r\n\r\n'" +
    f" | nc ::1 {ts.Variables.portv6} >> client.log" + " ; echo '======' >> client.log")
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = "grep -e '^===' -e '^HTTP/' -e 'X-Resp-Id:' -e '<HTML>' client.log"
tr.Processes.Default.Streams.stdout = 'client.gold'
tr.Processes.Default.ReturnCode = 0

server.Streams.All += Testers.ContainsExpression("Serving GET /sdfsdf/0... Finished", "Served 1")
server.Streams.All += Testers.ContainsExpression("Serving GET /sdfsdf/1... Finished", "Served 2")
server.Streams.All += Testers.ContainsExpression("Serving GET /example/2... Finished", "Served 3")

outserver = Test.Disk.File("outserver")
outserver.Content = "server.gold"
