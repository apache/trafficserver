'''
Test that a resumed TLS session's client cert is re-validated against the current SNI's CA.
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
Test that a TLS session ticket obtained under one verify_client: STRICT SNI
cannot be resumed under a co-hosted SNI that is also STRICT but trusts a
different certificate authority, while resuming under the issuing SNI works.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TlsTicketVerifyClientCaScope:
    '''Test that verify_client_ca_certs is enforced for a resumed TLS session.'''

    def __init__(self) -> None:
        '''Configure the origin server, ATS process, and test runs.'''
        Test.Setup.Copy('file.ticket')
        Test.Setup.Copy('ssl/aaa-signed.pem')
        Test.Setup.Copy('ssl/aaa-signed.key')
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self._configure_server()
        self._configure_ts()
        self._add_cross_ca_run()
        self._add_same_sni_run()
        self._add_log_assertions()

    def _configure_server(self) -> None:
        '''Configure the origin server with a simple response for each SNI's hostname.'''
        server = Test.MakeOriginServer('server')
        for host in ('aaa.example.com', 'bbb.example.com'):
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
        '''Configure the ATS process: one wildcard cert shared by two STRICT SNIs trusting different CAs.'''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.Setup.Copy('ssl/aaa-ca.pem', ts.Variables.SSLDir)
        ts.Setup.Copy('ssl/bbb-ca.pem', ts.Variables.SSLDir)

        # One wildcard cert entry shared by both SNIs below, so the only thing
        # separating the two tenants is the CA each one trusts.
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
                # Keep the access log from sitting in a buffer longer than the wait below.
                'proxy.config.log.max_secs_per_buffer': 1,
            })

        # Both tenants are STRICT, so the difference under test is purely which
        # CA signed the client certificate.
        ts.Disk.sni_yaml.AddLines(
            [
                'sni:',
                '- fqdn: aaa.example.com',
                '  verify_client: STRICT',
                '  verify_client_ca_certs:',
                f'    file: {ts.Variables.SSLDir}/aaa-ca.pem',
                '- fqdn: bbb.example.com',
                '  verify_client: STRICT',
                '  verify_client_ca_certs:',
                f'    file: {ts.Variables.SSLDir}/bbb-ca.pem',
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

    def _resume_command(self, issue_sni: str, resume_sni: str) -> str:
        '''
        Build a shell command that connects once with `issue_sni`, presenting the
        aaa-ca signed client certificate, to obtain a TLSv1.3 session ticket, then
        reconnects with `resume_sni`, presenting that ticket and no client
        certificate.

        :param issue_sni: SNI used for the first connection.
        :param resume_sni: SNI used for the second, ticket-resuming connection.
        :return: Shell command performing both connections.
        '''
        port = self.ts.Variables.ssl_port
        return (
            f'session_path=`mktemp` && '
            f'printf "GET / HTTP/1.1\\r\\nHost: {issue_sni}\\r\\nConnection: close\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {issue_sni} '
            f'-cert ./aaa-signed.pem -key ./aaa-signed.key '
            f'-sess_out "$$session_path" -tls1_3 -ign_eof && '
            f'printf "GET / HTTP/1.1\\r\\nHost: {resume_sni}\\r\\nConnection: close\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {resume_sni} -sess_in "$$session_path" -tls1_3 -ign_eof')

    def _add_cross_ca_run(self) -> None:
        '''A ticket issued under aaa.example.com must not resume under bbb.example.com, which trusts a different CA.'''
        tr = Test.AddTestRun('A ticket from aaa.example.com does not resume under bbb.example.com')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = self._resume_command('aaa.example.com', 'bbb.example.com')
        # The resumption attempt is rejected at the TLS layer before any HTTP
        # request is processed, so no particular client-side return code is
        # guaranteed here. Correctness is verified via the access log assertions
        # in _add_log_assertions() instead.

    def _add_same_sni_run(self) -> None:
        '''A ticket issued under aaa.example.com still resumes under aaa.example.com.'''
        tr = Test.AddTestRun('A ticket from aaa.example.com still resumes under aaa.example.com')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = self._resume_command('aaa.example.com', 'aaa.example.com')
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

        The cross-CA attempt never reaches the HTTP layer at all (rejected at the
        TLS layer), so it produces no access log line of its own; if the fix
        regressed, it would instead show up as a "200 bbb.example.com" line.
        '''
        tr = Test.AddTestRun('Wait for the access log')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Processes.Default.Command = (
            f"{os.path.join(Test.Variables.AtsTestToolsDir, 'condwait')} 60 3 -f "
            f"{os.path.join(self.ts.Variables.LOGDIR, 'squid.log')}")
        tr.Processes.Default.ReturnCode = 0

        self.ts.Disk.squid_log.Content = Testers.Lambda(
            lambda info, tester: TlsTicketVerifyClientCaScope.check_regex_count(
                self.ts.Disk.squid_log.AbsPath, r'^200 aaa\.example\.com$', 3,
                'The aaa.example.com requests (two issues and one same-SNI resumption) should succeed'))
        self.ts.Disk.squid_log.Content += Testers.Lambda(
            lambda info, tester: TlsTicketVerifyClientCaScope.check_regex_count(
                self.ts.Disk.squid_log.AbsPath, r'bbb\.example\.com', 0,
                'No request should reach the HTTP layer under bbb.example.com'))


TlsTicketVerifyClientCaScope()
