'''
Test that verify_client is still enforced for a resumed TLS session.
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
Test that a TLS session ticket obtained under a verify_client: NONE SNI
cannot be resumed under a different SNI configured verify_client: STRICT,
while resuming a ticket under the SNI that issued it still works.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TlsTicketVerifyClientScope:
    '''Test that verify_client is enforced for a resumed TLS session.'''

    def __init__(self) -> None:
        '''Configure the origin server, ATS process, and test runs.'''
        Test.Setup.Copy('file.ticket')
        Test.Setup.Copy('ssl/signed-foo.pem')
        Test.Setup.Copy('ssl/signed-foo.key')
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self._configure_server()
        self._configure_ts()
        self._add_cross_sni_run()
        self._add_same_sni_run()
        self._add_log_assertions()

    def _configure_server(self) -> None:
        '''Configure the origin server with a simple response for each SNI's hostname.'''
        server = Test.MakeOriginServer('server')
        for host in ('open.example.com', 'strict.example.com'):
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
        '''Configure the ATS process: one wildcard cert shared by both SNIs, one STRICT.'''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.addSSLfile('ssl/signer.pem')

        # One wildcard cert entry shared by both SNIs below, so any per-SNI scoping
        # under test comes from the fix, not from the two SNIs already having
        # separate cert contexts.
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.Port}')

        ts.Disk.records_config.update(
            {
                'proxy.config.url_remap.pristine_host_hdr': 1,
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.ssl.server.session_ticket.enable': 1,
                'proxy.config.ssl.server.ticket_key.filename': self.ticket_file,
                'proxy.config.ssl.CA.cert.filename': f'{ts.Variables.SSLDir}/signer.pem',
            })

        ts.Disk.sni_yaml.AddLines(
            [
                'sni:',
                '- fqdn: open.example.com',
                '  verify_client: NONE',
                '- fqdn: strict.example.com',
                '  verify_client: STRICT',
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
        self.ts = ts

    def _resume_command(self, issue_sni: str, issue_cert_args: str, resume_sni: str) -> str:
        '''
        Build a shell command that connects once with `issue_sni` (optionally
        presenting a client certificate) to obtain a TLSv1.3 session ticket, then
        reconnects with `resume_sni`, presenting that ticket and no client
        certificate.

        :param issue_sni: SNI used for the first connection.
        :param issue_cert_args: Extra openssl s_client args for the first connection.
        :param resume_sni: SNI used for the second, ticket-resuming connection.
        :return: Shell command performing both connections.
        '''
        port = self.ts.Variables.ssl_port
        return (
            f'session_path=`mktemp` && '
            f'printf "GET / HTTP/1.1\\r\\nHost: {issue_sni}\\r\\nConnection: close\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {issue_sni} {issue_cert_args} '
            f'-sess_out "$$session_path" -tls1_3 -ign_eof && '
            f'printf "GET / HTTP/1.1\\r\\nHost: {resume_sni}\\r\\nConnection: close\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {resume_sni} -sess_in "$$session_path" -tls1_3 -ign_eof')

    def _add_cross_sni_run(self) -> None:
        '''A ticket issued under open.example.com (NONE) must not resume under strict.example.com (STRICT).'''
        tr = Test.AddTestRun('A ticket from open.example.com does not resume under strict.example.com')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = self._resume_command('open.example.com', '', 'strict.example.com')
        # The resumption attempt is rejected at the TLS layer before any HTTP
        # request is processed, so no particular client-side return code is
        # guaranteed here (it depends on how the peer observes the aborted
        # connection). Correctness is verified via the access log assertions
        # in _add_log_assertions() instead.

    def _add_same_sni_run(self) -> None:
        '''A ticket issued under strict.example.com with a valid client cert still resumes under the same SNI.'''
        tr = Test.AddTestRun('A ticket from strict.example.com still resumes under strict.example.com')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = self._resume_command(
            'strict.example.com', '-cert ./signed-foo.pem -key ./signed-foo.key', 'strict.example.com')
        tr.ReturnCode = 0

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
        Assert on the access log status codes for every request across both runs.

        The cross-SNI attempt never reaches the HTTP layer at all (rejected at
        the TLS layer), so it produces no access log line of its own; if the
        fix regressed, it would instead show up as an extra
        "200 strict.example.com" line.
        '''
        self.ts.Disk.squid_log.Content = Testers.Lambda(
            lambda info, tester: TlsTicketVerifyClientScope.check_regex_count(
                self.ts.Disk.squid_log.AbsPath, r'^200 open\.example\.com$', 1, 'The open.example.com request should succeed'))
        self.ts.Disk.squid_log.Content += Testers.Lambda(
            lambda info, tester: TlsTicketVerifyClientScope.check_regex_count(
                self.ts.Disk.squid_log.AbsPath, r'^200 strict\.example\.com$', 2,
                'Only the legitimate strict.example.com requests (issue and same-SNI resumption) should succeed'))


TlsTicketVerifyClientScope()
