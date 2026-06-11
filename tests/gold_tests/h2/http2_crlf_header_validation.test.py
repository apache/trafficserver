'''
Verify HTTP/2 requests with NUL, CR, or LF in header values are rejected.
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

import sys

Test.Summary = 'HTTP/2 requests with NUL, CR, or LF in header values are rejected'
Test.ContinueOnFail = True

MALFORMED_CLIENT = 'malformed_h2_request_client.py'

CONTROL_CHARACTER_CASES = (
    {
        'scenario': 'crlf-in-header-value',
        'description': 'HTTP/2 request with CRLF in header value',
    },
    {
        'scenario': 'cr-in-header-value',
        'description': 'HTTP/2 request with bare CR in header value',
    },
    {
        'scenario': 'lf-in-header-value',
        'description': 'HTTP/2 request with bare LF in header value',
    },
    {
        'scenario': 'nul-in-header-value',
        'description': 'HTTP/2 request with NUL in header value',
    },
)

server = Test.MakeOriginServer('server')
server.Streams.All = Testers.ExcludesExpression(
    'x-injected',
    'Malformed control-character requests must not reach the origin server.',
)
server.Streams.All += Testers.ExcludesExpression(
    'malformed-nul-value',
    'Malformed NUL request must not reach the origin server.',
)
server.addResponse(
    'sessionlog.json', {
        'headers': 'GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n',
        'timestamp': '1469733493.993',
        'body': '',
    }, {
        'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
        'timestamp': '1469733493.993',
        'body': '',
    })

ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
ts.addDefaultSSLFiles()
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    })
ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}/')

Test.Setup.CopyAs('../connect/' + MALFORMED_CLIENT, Test.RunDirectory)

for i, case in enumerate(CONTROL_CHARACTER_CASES):
    tr = Test.AddTestRun(case['description'])
    tr.Processes.Default.Command = (f'{sys.executable} {MALFORMED_CLIENT} {ts.Variables.ssl_port} {case["scenario"]}')
    tr.Processes.Default.ReturnCode = 0
    if i == 0:
        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)
    tr.StillRunningAfter = ts
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        r'Received (RST_STREAM|GOAWAY|HTTP/2 response with status 4\d\d)',
        f'ATS should reject the request: {case["description"]}',
    )
