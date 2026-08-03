'''
Verify HTTP/3 over QMux interoperability with a Go client.
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

import os

Test.Summary = '''Verify that a Go QMux client can complete HTTP/3 transactions through ATS.'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_QMUX'),
    Condition.HasGoVersion('1.26'),
)


class TestQMuxGoClient:
    '''Configure a Go client interoperability test for HTTP/3 over QMux.'''

    replay_file: str = 'qmux.replay.yaml'

    def __init__(self) -> None:
        '''Configure the test run.'''
        tr = Test.AddTestRun('Go HTTP/3 over QMux client request')
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        self._configure_client(tr)

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        '''Configure the Proxy Verifier origin server.

        :param tr: The TestRun to add the server process to.
        :return: The server process.
        '''
        server = tr.AddVerifierServerProcess('server', self.replay_file, verbose=False)
        self._server = server
        return server

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        '''Configure Traffic Server.

        :param tr: The TestRun to add the Traffic Server process to.
        :return: The Traffic Server process.
        '''
        ts = tr.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
        self._ts = ts

        ts.StartupTimeout = 60
        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_yaml.AddLines(
            '''
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
'''.split('\n'))
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'qmux|http3',
                'proxy.config.http.server_ports': (f'{ts.Variables.port} {ts.Variables.ssl_port}:ssl:proto=h3qx-01'),
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')
        ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: qmux_access
      format: 'c_alpn=%<cqssa> client_version=%<cqpv> c_method=%<cqhm> c_url=%<cquuc>'

  logs:
    - filename: qmux_access
      format: qmux_access
'''.split('\n'))

        access_log = Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'qmux_access.log'), exists=True)
        access_log.Content = Testers.ContainsExpression(
            r'c_alpn=h3qx-01 client_version=http/3 c_method=GET '
            r'c_url=https://qmux\.example\.com:[0-9]+/qmux-get-empty',
            'ATS should log the empty QMux request as HTTP/3 over the h3qx-01 ALPN.')
        access_log.Content += Testers.ContainsExpression(
            r'c_alpn=h3qx-01 client_version=http/3 c_method=POST '
            r'c_url=https://qmux\.example\.com:[0-9]+/qmux-post-large',
            'ATS should log the large QMux request as HTTP/3 over the h3qx-01 ALPN.')
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        '''Configure the Go QMux client.

        :param tr: The TestRun to add the client process to.
        :return: The client process.
        '''
        tr.Setup.Copy('go_qmux_client')
        client = tr.Processes.Default
        client.Env['GOFLAGS'] = '-mod=readonly -modcacherw'
        client.Env['GOCACHE'] = os.path.join(tr.RunDirectory, 'gocache')
        client.Env['GOMODCACHE'] = os.path.join(tr.RunDirectory, 'gomodcache')
        client.Env['GOTOOLCHAIN'] = 'local'
        client.Command = (
            f'cd "{os.path.join(tr.RunDirectory, "go_qmux_client")}" && '
            f'go run . --addr 127.0.0.1:{self._ts.Variables.ssl_port} '
            f'--authority qmux.example.com:{self._ts.Variables.ssl_port} '
            '--server-name qmux.example.com')
        client.ReturnCode = 0
        client.Streams.stdout = Testers.ContainsExpression(
            'completed 3 QMux HTTP/3 requests: alpn=h3qx-01',
            'The Go client should complete all HTTP/3 requests over one QMux session.')
        client.StartBefore(self._server)
        client.StartBefore(self._ts)
        return client


TestQMuxGoClient()
