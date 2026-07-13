'''
Verify TLS signature-algorithm access log fields.
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

Test.Summary = '''
Verify offered and negotiated TLS signature-algorithm log fields.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TestTLSSignatureAlgorithms:
    '''Verify TLS signature-algorithm access log fields.'''

    _paths = ('/tls12-full', '/tls12-resumed', '/tls13-full', '/tls13-resumed')

    def __init__(self):
        '''Configure the test processes and handshake runs.'''
        self._server = self._configure_server()
        self._ts = self._configure_traffic_server()
        self._tls12_session = os.path.join(Test.RunDirectory, 'tls12.session')
        self._tls13_session = os.path.join(Test.RunDirectory, 'tls13.session')

        self._add_handshake_run(
            'Log a full TLS 1.2 handshake',
            '/tls12-full',
            '-tls1_2',
            'rsa_pkcs1_sha256',
            f'-sess_out {self._tls12_session}',
            start_processes=True)
        self._add_handshake_run(
            'Log a resumed TLS 1.2 handshake', '/tls12-resumed', '-tls1_2', 'rsa_pkcs1_sha256', f'-sess_in {self._tls12_session}')
        self._add_handshake_run(
            'Log a full TLS 1.3 handshake', '/tls13-full', '-tls1_3', 'rsa_pss_rsae_sha256:rsa_pkcs1_sha256',
            f'-sess_out {self._tls13_session}')
        self._add_handshake_run(
            'Log a resumed TLS 1.3 handshake',
            '/tls13-resumed',
            '-tls1_3',
            'rsa_pss_rsae_sha256:rsa_pkcs1_sha256',
            f'-sess_in {self._tls13_session}',
            keep_processes_running=False)

    def _configure_server(self) -> 'Process':
        '''Configure the origin server.

        :return: The origin server process.
        '''
        server = Test.MakeOriginServer('server')
        for path in self._paths:
            request = {
                'headers': f'GET {path} HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n',
                'timestamp': '1469733493.993',
                'body': '',
            }
            response = {
                'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n',
                'timestamp': '1469733493.993',
                'body': '',
            }
            server.addResponse('sessionlog.json', request, response)
        return server

    def _configure_traffic_server(self) -> 'Process':
        '''Configure Traffic Server.

        :return: The Traffic Server process.
        '''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        ts.Disk.ssl_multicert_yaml.AddLines(
            '''
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
'''.split('\n'))
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.session_ticket.enable': 1,
                'proxy.config.ssl.server.session_ticket.number': 2,
            })
        ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: tls_signature_algorithms
      format: '%<cqup> %<cqssv> %<cqssig> %<cqssin> %<cqssr>'
  logs:
    - filename: tls_signature_algorithms
      format: tls_signature_algorithms
'''.split('\n'))
        log_path = os.path.join(ts.Variables.LOGDIR, 'tls_signature_algorithms.log')
        Test.Disk.File(log_path, exists=True, content='gold/tls_signature_algorithms.gold')
        return ts

    def _s_client_command(self, path: str, tls_option: str, sigalgs: str, session_option: str) -> str:
        '''Build an OpenSSL client command for a handshake run.

        :param path: The request path.
        :param tls_option: The option selecting the TLS version.
        :param sigalgs: The signature algorithms to offer.
        :param session_option: The option for reading or writing a session.
        :return: The OpenSSL client command.
        '''
        request = f'GET {path} HTTP/1.1\\r\\nHost: example.com\\r\\nConnection: close\\r\\n\\r\\n'
        return (
            f'printf "{request}" | openssl s_client -quiet -connect 127.0.0.1:{self._ts.Variables.ssl_port} '
            f'-servername example.com {tls_option} -sigalgs {sigalgs} {session_option}')

    def _add_handshake_run(
            self,
            description: str,
            path: str,
            tls_option: str,
            sigalgs: str,
            session_option: str,
            start_processes: bool = False,
            keep_processes_running: bool = True) -> 'TestRun':
        '''Add a TLS handshake test run.

        :param description: The test run description.
        :param path: The request path.
        :param tls_option: The option selecting the TLS version.
        :param sigalgs: The signature algorithms to offer.
        :param session_option: The option for reading or writing a session.
        :param start_processes: Whether this run starts the server processes.
        :param keep_processes_running: Whether the server processes remain running afterward.
        :return: The configured TestRun.
        '''
        tr = Test.AddTestRun(description)
        tr.Processes.Default.Command = self._s_client_command(path, tls_option, sigalgs, session_option)
        tr.Processes.Default.ReturnCode = 0
        if start_processes:
            tr.Processes.Default.StartBefore(self._server)
            tr.Processes.Default.StartBefore(self._ts)
        if keep_processes_running:
            tr.StillRunningAfter = self._server
            tr.StillRunningAfter += self._ts
        return tr


TestTLSSignatureAlgorithms()
