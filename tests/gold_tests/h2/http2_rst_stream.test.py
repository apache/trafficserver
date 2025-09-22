'''
Abort HTTP/2 connection using RST_STREAM frame.
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
Abort HTTP/2 connection using RST_STREAM frame.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'), Condition.HasProxyVerifierVersion('2.8.0'))

#
# Client sends RST_STREAM after DATA frame
#
ts = Test.MakeATSProcess("ts0", enable_tls=True)
replay_file = "replay_rst_stream/http2_rst_stream_client_after_data.yaml"
server = Test.MakeVerifierServerProcess("server0", replay_file)
ts.addDefaultSSLFiles()
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.diags.debug.enabled': 3,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 4,
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
    })
ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.https_port}')
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

tr = Test.AddTestRun('Client sends RST_STREAM after DATA frame')
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client0", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])

tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'Submitting RST_STREAM frame for key 1 after DATA frame with error code INTERNAL_ERROR.', 'Detect client abort flag.')

tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'Submitted RST_STREAM frame for key 1 on stream 1.', 'Send RST_STREAM frame.')

server.Streams.All += Testers.ExcludesExpression('RST_STREAM', 'Server is not affected.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Received HEADERS frame', 'Received HEADERS frame.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Received DATA frame', 'Received DATA frame.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Received RST_STREAM frame', 'Received RST_STREAM frame.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Parsed RST_STREAM frame: Error Code: 2', 'Error Code: ')

#
# Client sends RST_STREAM after HEADERS frame
#
ts = Test.MakeATSProcess("ts1", enable_tls=True)
replay_file = "replay_rst_stream/http2_rst_stream_client_after_headers.yaml"
server = Test.MakeVerifierServerProcess("server1", replay_file)
ts.addDefaultSSLFiles()
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.diags.debug.enabled': 3,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 4,
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
    })
ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.https_port}')
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

tr = Test.AddTestRun('Client sends RST_STREAM after HEADERS frame')
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client1", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])

tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'Submitting RST_STREAM frame for key 1 after HEADERS frame with error code STREAM_CLOSED.', 'Detect client abort flag.')

tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'Submitted RST_STREAM frame for key 1 on stream 1', 'Send RST_STREAM frame.')

server.Streams.All += Testers.ExcludesExpression('RST_STREAM', 'Server is not affected.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Received HEADERS frame', 'Received HEADERS frame.')

ts.Disk.traffic_out.Content += Testers.ExcludesExpression('Received DATA frame', 'Received DATA frame.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Received RST_STREAM frame', 'Received RST_STREAM frame.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Parsed RST_STREAM frame: Error Code: 5', 'Error Code: ')

#
# Server sends RST_STREAM after HEADERS frame
#
ts = Test.MakeATSProcess("ts2", enable_tls=True)
replay_file = "replay_rst_stream/http2_rst_stream_server_after_headers.yaml"
server = Test.MakeVerifierServerProcess("server2", replay_file)
ts.addDefaultSSLFiles()
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.diags.debug.enabled': 3,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 1,
        'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
        'proxy.config.http.server_session_sharing.pool': 'thread',
        'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
    })
ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.https_port}')
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

tr = Test.AddTestRun('Server sends RST_STREAM after HEADERS frame')
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client2", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])

tr.Processes.Default.Streams.All += Testers.ContainsExpression(
    'Received RST_STREAM frame with stream id 1, error code 0', 'Client received RST_STREAM frame.')

server.Streams.All += "gold/server_after_headers.gold"

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Received RST_STREAM frame', 'Received RST_STREAM frame.')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Parsed RST_STREAM frame: Error Code: 11', 'Error Code: ')

ts.Disk.traffic_out.Content += Testers.ContainsExpression('Send RST_STREAM frame: Error Code: 0', 'Error Code: ')
