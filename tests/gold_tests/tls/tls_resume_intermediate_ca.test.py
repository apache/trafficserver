'''
Test that a resumed mTLS session survives when the client cert is issued by an intermediate CA.
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
import re

Test.Summary = '''
Test that a verify_client: STRICT SNI whose trust bundle holds only a root CA
keeps accepting a client certificate issued by an intermediate CA when the
client resumes its TLS session, on both TLS 1.2 and TLS 1.3 ticket resumption,
and that replacing that trust bundle in place retires the sessions it verified.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TlsResumeIntermediateCa:
    '''Test that ticket resumption works for a client cert that chains through an intermediate CA.'''

    _sni = 'int.example.com'
    _tls_versions = ('1.2', '1.3')

    def __init__(self) -> None:
        '''Configure the origin server, ATS process, and test runs.'''
        Test.Setup.Copy('file.ticket')
        Test.Setup.Copy('ssl/intclient.pem')
        Test.Setup.Copy('ssl/intclient.key')
        Test.Setup.Copy('ssl/intca-sub.pem')
        Test.Setup.Copy('ssl/aaa-ca.pem')
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self._configure_server()
        self._configure_ts()
        for version in self._tls_versions:
            self._add_resume_run(version)
        self._add_replace_ca_run()
        for version in self._tls_versions:
            self._add_stale_session_run(version)
        self._add_log_assertions()

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
        '''Configure the ATS process: a STRICT SNI that trusts only the root of a three-level client PKI.'''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.Setup.Copy('ssl/intca-root.pem', ts.Variables.SSLDir)

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
                'proxy.config.ssl.TLSv1_3.enabled': 1,
                'proxy.config.log.max_secs_per_buffer': 1,
            })

        # Only the root is trusted. The intermediate that issued the client
        # certificate is expected to arrive from the client in the handshake.
        ts.Disk.sni_yaml.AddLines(
            [
                'sni:',
                f'- fqdn: {self._sni}',
                '  verify_client: STRICT',
                '  verify_client_ca_certs:',
                f'    file: {ts.Variables.SSLDir}/intca-root.pem',
            ])

        ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: testformat
      format: '%<pssc> %<{Host}cqh>'
  logs:
    - mode: ascii
      format: testformat
      filename: squid
'''.split("\n"))

        ts.Disk.diags_log.Content = Testers.ExcludesExpression(
            'rejecting a resumed TLS session', 'A resumed session with a valid intermediate-issued cert must not be rejected')
        self.ts = ts

    def _session_path(self, tls_version: str) -> str:
        '''
        Return the path of the file holding the saved session for a TLS version.

        :param tls_version: The TLS version the session was negotiated with, '1.2' or '1.3'.
        :return: Path of the session file.
        '''
        return os.path.join(Test.RunDirectory, f'session-{tls_version}')

    def _client_command(self, tls_version: str, session_arg: str) -> str:
        '''
        Build a shell command that connects once, presenting the intermediate-issued
        client certificate along with its intermediate, and sends one request.

        :param tls_version: The TLS version to pin the connection to, '1.2' or '1.3'.
        :param session_arg: The s_client argument saving or offering a session.
        :return: Shell command performing the connection.
        '''
        port = self.ts.Variables.ssl_port
        version_flag = f'-tls1_{tls_version.split(".")[1]}'
        cert_args = '-cert ./intclient.pem -key ./intclient.key -cert_chain ./intca-sub.pem'
        request = f'printf "GET / HTTP/1.1\\r\\nHost: {self._sni}\\r\\nConnection: close\\r\\n\\r\\n"'
        return (
            f'{request} | openssl s_client -connect 127.0.0.1:{port} -servername {self._sni} {cert_args} '
            f'{session_arg} {version_flag} -ign_eof')

    def _resume_command(self, tls_version: str) -> str:
        '''
        Build a shell command that obtains a session ticket and then reconnects
        with that ticket to resume the session.

        :param tls_version: The TLS version to pin both connections to, '1.2' or '1.3'.
        :return: Shell command performing both connections.
        '''
        session_path = self._session_path(tls_version)
        return (
            f'{self._client_command(tls_version, f"-sess_out {session_path}")} && '
            f'{self._client_command(tls_version, f"-sess_in {session_path}")}')

    def _add_resume_run(self, tls_version: str) -> None:
        '''
        Add a test run that issues and then resumes a session over the given TLS version.

        :param tls_version: The TLS version to pin both connections to, '1.2' or '1.3'.
        '''
        tr = Test.AddTestRun(f'Resume a TLSv{tls_version} session carrying an intermediate-issued client cert')
        if tls_version == self._tls_versions[0]:
            tr.Processes.Default.StartBefore(self.server)
            tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = self._resume_command(tls_version)
        tr.ReturnCode = 0
        # Resumption is negotiated before ATS decides whether to keep the
        # connection, so "Reused" alone only proves the ticket was accepted by
        # the TLS library. The access log count below proves the resumed
        # request was actually served.
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'Reused, TLSv{tls_version}', 'The second connection should resume the session')

    def _add_replace_ca_run(self) -> None:
        '''
        Replace the trusted root in place with an unrelated CA.

        The bundle keeps its path, so only its contents distinguish the trust
        anchors that verified the saved sessions from the ones now in force.
        '''
        tr = Test.AddTestRun('Replace the trusted root CA bundle in place')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Processes.Default.Command = f'cp ./aaa-ca.pem {self.ts.Variables.SSLDir}/intca-root.pem'
        tr.Processes.Default.ReturnCode = 0

    def _add_stale_session_run(self, tls_version: str) -> None:
        '''
        Add a test run that offers a session verified under the replaced CA bundle.

        The session must not be resumed. The full handshake that follows presents
        the client certificate again, which the new bundle does not trust, so no
        request is served.

        :param tls_version: The TLS version the saved session was negotiated with, '1.2' or '1.3'.
        '''
        tr = Test.AddTestRun(f'A TLSv{tls_version} session verified by the replaced CA does not resume')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = self._client_command(tls_version, f'-sess_in {self._session_path(tls_version)}')
        # The two Excludes below would also hold if the client never reached ATS, which matters
        # because this runs right after the bundle was replaced. These two prove it did: a
        # negotiated full handshake, then ATS refusing the certificate for the expected reason.
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'New, TLSv{tls_version}', 'The client must reach ATS and negotiate a full handshake')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'tlsv1 alert unknown ca', 'ATS must refuse the certificate the replaced bundle no longer trusts')
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            f'Reused, TLSv{tls_version}', 'A session verified by a CA that is no longer trusted must not resume')
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            'HTTP/1.1 200', 'A client certificate the current bundle does not trust must not be served')

    @staticmethod
    def check_regex_count(output_path: str, pattern: str, expected_count: int, description: str) -> tuple[bool, str, str]:
        '''
        Count regex matches in a process output file.

        :param output_path: Path to the output file to inspect.
        :param pattern: Regex pattern to count.
        :param expected_count: Expected number of matches.
        :param description: Description reported by the tester.
        :return: AuTest lambda result tuple.
        '''
        with open(output_path, 'r') as f:
            content = f.read()

        matches = re.findall(pattern, content, re.MULTILINE)
        if len(matches) == expected_count:
            return (True, description, f'Found {len(matches)} matches for {pattern}')
        return (False, description, f'Expected {expected_count} matches for {pattern}, found {len(matches)}')

    def _add_log_assertions(self) -> None:
        '''
        Assert that every request from the fresh and resumed connections reached
        the origin, and that nothing else did.

        A rejected resumption never reaches the HTTP layer, so it shows up as a
        missing access log line rather than as an error status, while a stale
        session that wrongly resumed would add a line.
        '''
        tr = Test.AddTestRun('Wait for the access log')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Processes.Default.Command = (
            f"{os.path.join(Test.Variables.AtsTestToolsDir, 'condwait')} 60 3 -f "
            f"{os.path.join(self.ts.Variables.LOGDIR, 'squid.log')}")
        tr.Processes.Default.ReturnCode = 0

        expected = 2 * len(self._tls_versions)
        self.ts.Disk.squid_log.Content = Testers.Lambda(
            lambda info, tester: TlsResumeIntermediateCa.check_regex_count(
                self.ts.Disk.squid_log.AbsPath, rf'^200 {re.escape(self._sni)}$', expected,
                'Every fresh and resumed request, and only those, should be served'))


TlsResumeIntermediateCa()
