'''
Test that a TLS session is not resumed after its SNI's verify_client level changes.
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
Test that raising verify_client from MODERATE to STRICT for one SNI stops that
SNI's existing sessions from resuming, holding the server name and the CA
bundle constant so that the verify mode is the only thing that changed.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TlsResumeVerifyLevel:
    '''
    Test that the verify mode is part of the session resumption scope.

    The other scope tests vary verify_client across two different SNIs, where
    the server name alone would carry them. This one keeps a single SNI and a
    single CA bundle and changes only the level, so it fails if the verify mode
    term is dropped from the session id context.
    '''

    _sni = 'level.example.com'

    def __init__(self) -> None:
        '''Configure the origin server, ATS process, and test runs.'''
        Test.Setup.Copy('file.ticket')
        Test.Setup.Copy('ssl/aaa-signed.pem')
        Test.Setup.Copy('ssl/aaa-signed.key')
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self.session_path = os.path.join(Test.RunDirectory, 'session')
        self._configure_server()
        self._configure_ts()
        self._add_resume_run()
        self._add_raise_level_run()
        self._add_stale_session_run()

    def _configure_server(self) -> None:
        '''Configure the origin server with a simple response for the SNI's hostname.'''
        server = Test.MakeOriginServer('server')
        request_header = {
            'headers': f'GET / HTTP/1.1\r\nHost: {self._sni}\r\nConnection: close\r\n\r\n',
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
        '''Configure the ATS process with one MODERATE SNI and a fixed CA bundle.'''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.Setup.Copy('ssl/aaa-ca.pem', ts.Variables.SSLDir)
        self.ca_path = os.path.join(ts.Variables.SSLDir, 'aaa-ca.pem')

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
            })

        self.sni_path = os.path.join(ts.Variables.CONFIGDIR, 'sni.yaml')
        ts.Disk.sni_yaml.AddLines(
            [
                'sni:',
                f'- fqdn: {self._sni}',
                '  verify_client: MODERATE',
                '  verify_client_ca_certs:',
                f'    file: {self.ca_path}',
            ])
        self.ts = ts

    def _client_command(self, session_arg: str) -> str:
        '''
        Build a shell command that connects once presenting the aaa-ca signed client cert.

        :param session_arg: The s_client argument saving or offering a session.
        :return: Shell command performing the connection.
        '''
        port = self.ts.Variables.ssl_port
        request = f'printf "GET / HTTP/1.1\\r\\nHost: {self._sni}\\r\\nConnection: close\\r\\n\\r\\n"'
        return (
            f'{request} | openssl s_client -connect 127.0.0.1:{port} -servername {self._sni} '
            f'-cert ./aaa-signed.pem -key ./aaa-signed.key {session_arg} -tls1_3 -ign_eof')

    def _add_resume_run(self) -> None:
        '''Obtain a session under MODERATE and resume it while the level is unchanged.'''
        tr = Test.AddTestRun('A session resumes while verify_client is unchanged')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = (
            f'{self._client_command(f"-sess_out {self.session_path}")} && '
            f'{self._client_command(f"-sess_in {self.session_path}")}')
        tr.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            'Reused, TLSv1.3', 'A session should resume while its scope is unchanged')

    def _add_raise_level_run(self) -> None:
        '''Raise verify_client to STRICT for the same SNI, keeping the CA bundle identical.'''
        tr = Test.AddTestRun('Raise verify_client from MODERATE to STRICT')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        sni = '\n'.join(
            [
                'sni:',
                f'- fqdn: {self._sni}',
                '  verify_client: STRICT',
                '  verify_client_ca_certs:',
                f'    file: {self.ca_path}',
            ])
        tr.Processes.Default.Command = f"printf '%s\\n' '{sni}' > {self.sni_path}"
        tr.Processes.Default.ReturnCode = 0

        tr = Test.AddConfigReload(self.ts, expect_tasks=['sni.yaml'], description='Reload the new verify_client level')
        tr.StillRunningAfter += self.server

    def _add_stale_session_run(self) -> None:
        '''
        Offer the session issued under MODERATE now that the SNI is STRICT.

        The client still presents a certificate the bundle trusts, so the full
        handshake succeeds. Only the resumption must be refused.
        '''
        tr = Test.AddTestRun('A session issued under MODERATE does not resume under STRICT')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = self._client_command(f'-sess_in {self.session_path}')
        tr.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ExcludesExpression(
            'Reused', 'A session must not resume after its SNI verify_client level changed')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'HTTP/1.1 200', 'The full handshake that follows should still be served')


TlsResumeVerifyLevel()
