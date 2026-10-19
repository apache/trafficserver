'''
Verify TLS connections using PROXY protocol do not require ALPN negotiation.
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

Test.Summary = __doc__

server = Test.MakeOriginServer('server')
ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False, enable_proxy_protocol=True)
ts.addDefaultSSLFiles()

server.addResponse(
    'sessionlog.json',
    {'headers': 'GET / HTTP/1.1\r\nHost: no-alpn.example.com\r\n\r\n'},
    {'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\n', 'body': 'OK'})

ts.Disk.ssl_multicert_yaml.AddLines(
    [
        'ssl_multicert:',
        '  - dest_ip: "*"',
        '    ssl_cert_name: server.pem',
        '    ssl_key_name: server.key',
        '',
    ])
ts.Disk.records_config.update(
    {
        'proxy.config.http.proxy_protocol_allowlist': '127.0.0.1',
        'proxy.config.http.insert_response_via_str': 0,
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    })
ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}/')

tr = Test.AddTestRun('TLS with PROXY protocol and no ALPN')
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = (
    f"curl -k -sS --http1.1 --no-alpn --haproxy-protocol -o /dev/null -w '%{{http_code}}' "
    f"'https://no-alpn.example.com:{ts.Variables.proxy_protocol_ssl_port}/' "
    f"--resolve 'no-alpn.example.com:{ts.Variables.proxy_protocol_ssl_port}:127.0.0.1'")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    '200', 'TLS with PROXY protocol succeeds without ALPN negotiation')
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
