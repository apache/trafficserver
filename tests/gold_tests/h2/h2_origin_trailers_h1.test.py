'''
Verify HTTP/2 origin trailers are not forwarded to HTTP/1 clients.
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

Test.Summary = __doc__

Test.SkipUnless(Condition.HasProxyVerifierVersion('2.8.0'))


class TestOriginTrailers:
    """Verify HTTP/2 origin trailers are handled safely by client protocol."""

    _replay_file = 'h2_origin_trailers_h1.replay.yaml'

    def __init__(self):
        self._h1_server = self._configure_server('h2-origin-h1')
        self._h1_ts = self._configure_ats('ts-h1', self._h1_server)
        self._configure_h1_client()

        self._h2_server = self._configure_server('h2-origin-h2')
        self._h2_ts = self._configure_ats('ts-h2', self._h2_server)
        self._configure_h2_client()

    def _configure_server(self, name):
        return Test.MakeVerifierServerProcess(name, self._replay_file)

    def _configure_ats(self, name, server):
        ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=False)
        ts.addDefaultSSLFiles()

        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|http2',
                'proxy.config.exec_thread.autoconfig.enabled': 0,
                'proxy.config.exec_thread.limit': 1,
                'proxy.config.http.server_session_sharing.pool': 'thread',
                'proxy.config.http.server_session_sharing.match': 'ip,sni,cert',
                'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })

        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.https_port}')
        return ts

    def _configure_h1_client(self):
        Test.Setup.CopyAs('h1_trailer_client.py', Test.RunDirectory)

        tr = Test.AddTestRun('HTTP/2 origin trailers are dropped for HTTP/1 clients')
        tr.Processes.Default.StartBefore(self._h1_server)
        tr.Processes.Default.StartBefore(self._h1_ts)
        tr.Processes.Default.Command = f'{sys.executable} h1_trailer_client.py 127.0.0.1 {self._h1_ts.Variables.port}'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'No H2 origin trailers were forwarded to the HTTP/1 client.', 'The HTTP/1 response must end at its terminal chunk.')

    def _configure_h2_client(self):
        tr = Test.AddTestRun('HTTP/2 origin trailers are forwarded to HTTP/2 clients')
        client = tr.AddVerifierClientProcess('h2-client', self._replay_file, https_ports=[self._h2_ts.Variables.ssl_port])
        client.StartBefore(self._h2_server)
        client.StartBefore(self._h2_ts)
        client.Streams.All += Testers.ContainsExpression(
            'x-ats-h2-trailer: smuggled', 'The HTTP/2 client must receive the origin trailer.')


TestOriginTrailers()
