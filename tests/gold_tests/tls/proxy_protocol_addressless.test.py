'''
Verify handling of addressless PROXY protocol headers.
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

Test.Summary = '''
Verify listeners handle addressless PROXY protocol headers.
'''

host = 'addressless.proxy.protocol.test'


def add_addressless_proxy_protocol_run(protocol_version: int, description: str, use_tls: bool) -> None:
    """Add a test run for an addressless PROXY header."""
    mode = 'tls' if use_tls else 'http'
    ts = Test.MakeATSProcess(f'ts_{mode}_v{protocol_version}', enable_tls=use_tls, enable_cache=False, enable_proxy_protocol=True)
    server = Test.MakeOriginServer(f'server_{mode}_v{protocol_version}')
    server.ReturnCode = 0

    request_header = {
        "headers": f"GET /proxy_protocol HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "ok"}
    server.addResponse("sessionlog.json", request_header, response_header)

    if use_tls:
        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

    ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}/')
    records_config = {
        'proxy.config.http.proxy_protocol_allowlist': '127.0.0.1',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'proxyprotocol',
    }
    if use_tls:
        records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })
    ts.Disk.records_config.update(records_config)

    tr = Test.AddTestRun(description)
    tr.TimeOut = 10
    tr.Setup.Copy('proxy_protocol_client.py')
    port = ts.Variables.proxy_protocol_ssl_port if use_tls else ts.Variables.proxy_protocol_port
    tr.Processes.Default.Command = (
        f'{sys.executable} proxy_protocol_client.py 127.0.0.1 {port} {host} '
        f'127.0.0.1 127.0.0.1 60123 {server.Variables.Port} '
        f'{protocol_version} --addressless')
    if use_tls:
        tr.Processes.Default.Command += ' --https'
    tr.Processes.Default.StartBefore(server)
    tr.Processes.Default.StartBefore(ts)
    tr.ReturnCode = 0
    tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Verify a successful response is received")


add_addressless_proxy_protocol_run(1, 'PROXY v1 UNKNOWN before HTTP/1', use_tls=False)
add_addressless_proxy_protocol_run(2, 'PROXY v2 LOCAL before HTTP/1', use_tls=False)
add_addressless_proxy_protocol_run(1, 'PROXY v1 UNKNOWN before TLS', use_tls=True)
add_addressless_proxy_protocol_run(2, 'PROXY v2 LOCAL before TLS', use_tls=True)
