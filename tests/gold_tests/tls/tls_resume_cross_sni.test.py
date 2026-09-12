'''
Test that a TLS session is not resumed under an SNI other than the one that issued it.
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
Test that a session issued under one SNI is not resumed under a different SNI
sharing the same listener and certificate, for SNIs that set no verify_client
at all, while resumption under the issuing SNI still works.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TlsResumeCrossSni:
    '''
    Test that session resumption is bound to the SNI that issued the session.

    Neither hostname appears in sni.yaml, so neither has a verify_client
    setting. That is the default configuration and the one in which nothing
    else scopes a session to its SNI, so it is what proves the binding is
    applied unconditionally rather than only when verify_client is present.
    '''

    _issuing_sni = 'alpha.example.com'
    _other_sni = 'beta.example.com'
    _tls_versions = ('1.2', '1.3')

    def __init__(self) -> None:
        '''Configure the origin server, ATS process, and test runs.'''
        Test.Setup.Copy('file.ticket')
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self._configure_server()
        self._configure_ts()
        for version in self._tls_versions:
            self._add_same_sni_run(version)
            self._add_cross_sni_run(version)
        self._add_case_fold_run()

    def _configure_server(self) -> None:
        '''Configure the origin server with a simple response for each hostname.'''
        server = Test.MakeOriginServer('server')
        for host in (self._issuing_sni, self._other_sni):
            request_header = {
                'headers': f'GET / HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n',
                'timestamp': '1469733493.993',
                'body': ''
            }
            response_header = {
                'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
                'timestamp': '1469733493.993',
                'body': 'hello'
            }
            server.addResponse('sessionlog.json', request_header, response_header)
        self.server = server

    def _configure_ts(self) -> None:
        '''Configure the ATS process: one wildcard cert serving both hostnames, no sni.yaml entries.'''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')

        # A single wildcard entry, so the only thing separating the two names is
        # the SNI itself rather than a distinct certificate context.
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.Port}')

        ts.Disk.records_config.update(
            {
                'proxy.config.url_remap.pristine_host_hdr': 1,
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.ssl.server.session_ticket.enable': 1,
                'proxy.config.ssl.server.ticket_key.filename': self.ticket_file,
                'proxy.config.ssl.TLSv1_2': 1,
                'proxy.config.ssl.TLSv1_3': 1,
            })

        # Deliberately no sni.yaml entries for either hostname: verify_client is
        # left unset, which is the case that has no VerifyClient SNI action.
        self.ts = ts

    def _session_path(self, tls_version: str, label: str) -> str:
        '''
        Return the path of the file holding a saved session.

        :param tls_version: The TLS version the session was negotiated with.
        :param label: Distinguishes the sessions saved by different runs.
        :return: Path of the session file.
        '''
        return os.path.join(Test.RunDirectory, f'session-{label}-{tls_version}')

    def _client_command(self, tls_version: str, sni: str, session_arg: str) -> str:
        '''
        Build a shell command that connects once with the given SNI and sends one request.

        :param tls_version: The TLS version to pin the connection to, '1.2' or '1.3'.
        :param sni: The SNI and Host header to use.
        :param session_arg: The s_client argument saving or offering a session.
        :return: Shell command performing the connection.
        '''
        port = self.ts.Variables.ssl_port
        version_flag = f'-tls1_{tls_version.split(".")[1]}'
        request = f'printf "GET / HTTP/1.1\\r\\nHost: {sni}\\r\\nConnection: close\\r\\n\\r\\n"'
        return (
            f'{request} | openssl s_client -connect 127.0.0.1:{port} -servername {sni} '
            f'{session_arg} {version_flag} -ign_eof')

    def _add_same_sni_run(self, tls_version: str) -> None:
        '''
        Add a control run proving tickets are issued and resumable under one SNI.

        Without this, a cross-SNI run that fails to resume would prove nothing,
        since it would also pass if resumption were broken outright.

        :param tls_version: The TLS version to pin both connections to.
        '''
        path = self._session_path(tls_version, 'same')
        tr = Test.AddTestRun(f'A TLSv{tls_version} session resumes under the SNI that issued it')
        if tls_version == self._tls_versions[0]:
            tr.Processes.Default.StartBefore(self.server)
            tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = (
            f'{self._client_command(tls_version, self._issuing_sni, f"-sess_out {path}")} && '
            f'{self._client_command(tls_version, self._issuing_sni, f"-sess_in {path}")}')
        tr.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'Reused, TLSv{tls_version}', 'A session offered under its issuing SNI should resume')

    def _add_case_fold_run(self) -> None:
        '''
        Add a run offering a session under the same SNI spelled with different case.

        sni.yaml matching lowercases the server name, so both spellings select
        one policy and must therefore land in one resumption scope.
        '''
        path = self._session_path('1.3', 'fold')
        tr = Test.AddTestRun('A session resumes under the same SNI spelled with different case')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = (
            f'{self._client_command("1.3", self._issuing_sni, f"-sess_out {path}")} && '
            f'{self._client_command("1.3", self._issuing_sni.upper(), f"-sess_in {path}")}')
        tr.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            'Reused, TLSv1.3', 'Case differences in the SNI must not split the resumption scope')

    def _add_cross_sni_run(self, tls_version: str) -> None:
        '''
        Add a run offering a session under a different SNI than issued it.

        :param tls_version: The TLS version to pin both connections to.
        '''
        path = self._session_path(tls_version, 'cross')
        tr = Test.AddTestRun(f'A TLSv{tls_version} session does not resume under a different SNI')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = (
            f'{self._client_command(tls_version, self._issuing_sni, f"-sess_out {path}")} && '
            f'{self._client_command(tls_version, self._other_sni, f"-sess_in {path}")}')
        tr.ReturnCode = 0
        # The second connection falls back to a full handshake, which succeeds
        # because neither SNI requires a client certificate. So the resumption
        # is what is under test here, not whether the request is served.
        tr.Processes.Default.Streams.All = Testers.ExcludesExpression(
            'Reused', 'A session must not resume under an SNI other than the one that issued it')


TlsResumeCrossSni()
