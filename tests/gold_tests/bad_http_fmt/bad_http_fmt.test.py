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

ArbitraryTimestamp = '12345678'

random_path = "/sdfsdf"

random_method = "xyzxyz"

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|dns',
    'proxy.config.url_remap.remap_required': 0,
    'proxy.config.http.strict_uri_parsing': 1,
    'proxy.config.http.cache.http': 0,
})

# Allow random_method for 128.0.0.1, do not allow it for ::1.
#
ts.Disk.ip_allow_yaml.AddLines([
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
    '      - GET'
])

Test.GetTcpPort("server_port")


def server_cmd(resp_id):
    dq = '"'
    return (fr"(nc -o server{resp_id}.log " +
            fr"--sh-exec 'sleep 1 ; printf {dq}HTTP/1.1 200 OK\r\n" +
            fr"X-Resp-Id: {resp_id}\r\n" +
            fr"Content-Length: 0\r\n\r\n{dq}' " +
            fr"-l 127.0.0.1 {Test.Variables.server_port} & )")


# Even if the request from the client is HTTP version 1.0, ATS's request to server will be HTTP version 1.1.
#
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = (
    server_cmd(1) +
    fr" ; printf 'GET {random_path}HTTP/1.0\r\n" +
    fr"Host: localhost:{Test.Variables.server_port}\r\n" +
    r"X-Req-Id: 0\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    server_cmd(2) +
    fr" ; printf 'GET {random_path}HTTP/1.1\r\n" +
    fr"Host: localhost:{Test.Variables.server_port}\r\n" +
    r"X-Req-Id: 1\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    fr"printf 'GET {random_path}<HTTP/1.1\r\n" +
    fr"Host: localhost:{Test.Variables.server_port}\r\n" +
    r"X-Req-Id: 2\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    fr"printf 'GET {random_path} HTTP/1.2\r\n" +
    fr"Host: localhost:{Test.Variables.server_port}\r\n" +
    r"X-Req-Id: 3\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    fr"printf 'GET {random_path} HTTP/0.9\r\n" +
    r"X-Req-Id: 4\r\n\r\n'" +
    fr" | nc localhost {ts.Variables.port} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    fr"printf 'GET {random_path} HTTP/0.9\r\n" +
    fr"Host: localhost:{Test.Variables.server_port}\r\n" +
    r"X-Req-Id: 5\r\n\r\n'" +
    fr"| nc localhost {ts.Variables.port} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    server_cmd(3) +
    fr" ; printf '{random_method} /example HTTP/1.1\r\n" +
    fr"Host: localhost:{Test.Variables.server_port}\r\n" +
    r"X-Req-Id: 6\r\n\r\n'" +
    f" | nc localhost {ts.Variables.port} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    server_cmd(4) +
    r" ; printf 'GET /example HTTP/1.1\r\n" +
    fr"Host: localhost:{Test.Variables.server_port}\r\n" +
    r"X-Req-Id: 7\r\n\r\n'" +
    f" | nc ::1 {ts.Variables.portv6} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    fr"printf '{random_method} /example HTTP/1.1\r\n" +
    fr"Host: localhost:{Test.Variables.server_port}\r\n\r\n'" +
    f" | nc ::1 {ts.Variables.portv6} >> client.log" +
    " ; echo '======' >> client.log"
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = "grep -e '^===' -e '^HTTP/' -e 'X-Resp-Id:' -e '<HTML>' client.log"
tr.Processes.Default.Streams.stdout = 'client.gold'
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = "grep -e 'X-Req-Id:' -e 'HTTP/' -e 'Content-' server1.log"
for n in range(2, 5):
    tr.Processes.Default.Command += f" ; grep -e 'X-Req-Id:' -e 'HTTP/' -e '[Cc]ontent-' server{n}.log"
tr.Processes.Default.Streams.stdout = 'server.gold'
tr.Processes.Default.ReturnCode = 0
