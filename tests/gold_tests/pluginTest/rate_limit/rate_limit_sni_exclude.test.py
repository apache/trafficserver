'''
Test that the rate_limit SNI exclude list exempts a client from rate limiting
without rejecting the TLS handshake.
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

Test.SkipUnless(Condition.PluginExists('rate_limit.so'))

server = Test.MakeOriginServer('server')
ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
ts.addDefaultSSLFiles()

server.addResponse(
    'sessionlog.json', {'headers': 'GET / HTTP/1.1\r\nHost: excluded.example.com\r\n\r\n'}, {
        'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\n',
        'body': 'OK'
    })
server.addResponse(
    'sessionlog.json', {'headers': 'GET / HTTP/1.1\r\nHost: limited.example.com\r\n\r\n'}, {
        'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\n',
        'body': 'OK'
    })

rate_limit_yaml = f'{ts.Variables.CONFIGDIR}/rate_limit.yaml'
ts.Disk.File(
    rate_limit_yaml, typename='ats:config').AddLines(
        [
            'lists:',
            '  - name: loopback',
            '    cidr:',
            '      - 127.0.0.0/8',
            'selector:',
            '  - sni: excluded.example.com',
            '    limit: 1',
            '    exclude: loopback',
            '  - sni: limited.example.com',
            '    limit: 1',
            '',
        ])

ts.Disk.ssl_multicert_yaml.AddLines(
    [
        'ssl_multicert:',
        '  - dest_ip: "*"',
        '    ssl_cert_name: server.pem',
        '    ssl_key_name: server.key',
    ])
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rate_limit',
        'proxy.config.http.insert_response_via_str': 0,
        'proxy.config.url_remap.remap_required': 0,
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    })
ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}/')
ts.Disk.plugin_config.AddLine(f'rate_limit.so {rate_limit_yaml}')
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    'Limiter on excluded.example.com is excluded via List=loopback', 'the excluded SNI matched the configured client IP list')

tr = Test.AddTestRun('SNI exclude list allows the TLS handshake')
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = (
    f"curl -sk -o /dev/null -w '%{{http_code}}' "
    f"'https://excluded.example.com:{ts.Variables.ssl_port}/' "
    f"--resolve 'excluded.example.com:{ts.Variables.ssl_port}:127.0.0.1' "
    f"&& curl -sk -o /dev/null -w '%{{http_code}}' "
    f"'https://limited.example.com:{ts.Variables.ssl_port}/' "
    f"--resolve 'limited.example.com:{ts.Variables.ssl_port}:127.0.0.1'")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    '200200', 'excluded and ordinary limited SNIs both complete TLS successfully')
