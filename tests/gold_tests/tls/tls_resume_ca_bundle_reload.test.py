'''
Test that a resumed mTLS session is retired when the global client CA bundle is replaced and reloaded.
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
Test that a session verified against proxy.config.ssl.CA.cert.filename under a
verify_client: STRICT SNI stops resuming once that bundle is replaced in place
and the configuration is reloaded, while the same session resumes before the
change.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TlsResumeCaBundleReload:
    '''Test that resumption is bound to the contents of the global client CA bundle.'''

    _sni = 'strict.example.com'
    _bundle_name = 'client-ca.pem'

    def __init__(self) -> None:
        '''Configure the origin server, ATS process, and test runs.'''
        Test.Setup.Copy('file.ticket')
        Test.Setup.Copy('ssl/aaa-signed.pem')
        Test.Setup.Copy('ssl/aaa-signed.key')
        Test.Setup.Copy('ssl/bbb-ca.pem')
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self.session_path = os.path.join(Test.RunDirectory, 'session')
        self._configure_server()
        self._configure_ts()
        self._add_resume_run()
        self._add_replace_bundle_run()
        self._add_stale_session_run()
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

        # A second, identical origin exists only so that the run after the reload has a process to
        # start, which is what carries the When.FileContains() wait for the rebuild.
        reload_server = Test.MakeOriginServer('reload_server')
        reload_server.addResponse('sessionlog.json', request_header, response_header)
        self.reload_server = reload_server

    def _configure_ts(self) -> None:
        '''Configure the ATS process: a STRICT SNI relying on the global client CA bundle.'''
        # traffic_manager is required for traffic_ctl config reload on this branch,
        # which v10 no longer needs.
        ts = Test.MakeATSProcess('ts', command='traffic_manager', enable_tls=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        self.bundle_path = os.path.join(ts.Variables.SSLDir, self._bundle_name)
        ts.Setup.CopyAs('ssl/aaa-ca.pem', ts.Variables.SSLDir, self._bundle_name)

        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.Port}')

        ts.Disk.records_config.update(
            {
                'proxy.config.url_remap.pristine_host_hdr': 1,
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.CA.cert.filename': self.bundle_path,
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.ssl.server.session_ticket.enable': 1,
                'proxy.config.ssl.server.ticket_key.filename': self.ticket_file,
                'proxy.config.log.max_secs_per_buffer': 1,
            })

        # No verify_client_ca_certs, so the client certificate is verified
        # against the global bundle loaded into the SSL_CTX.
        ts.Disk.sni_yaml.AddLines([
            'sni:',
            f'- fqdn: {self._sni}',
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

    def _client_command(self, session_arg: str) -> str:
        '''
        Build a shell command that connects once over TLSv1.3, presenting the
        aaa-ca signed client certificate, and sends one request.

        :param session_arg: The s_client argument saving or offering a session.
        :return: Shell command performing the connection.
        '''
        port = self.ts.Variables.ssl_port
        request = f'printf "GET / HTTP/1.1\\r\\nHost: {self._sni}\\r\\nConnection: close\\r\\n\\r\\n"'
        return (
            f'{request} | openssl s_client -connect 127.0.0.1:{port} -servername {self._sni} '
            f'-cert ./aaa-signed.pem -key ./aaa-signed.key {session_arg} -tls1_3 -ign_eof')

    def _add_resume_run(self) -> None:
        '''Obtain a session ticket and resume it while the issuing CA is still trusted.'''
        tr = Test.AddTestRun('Resume a session verified against the current CA bundle')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = (
            f'{self._client_command(f"-sess_out {self.session_path}")} && '
            f'{self._client_command(f"-sess_in {self.session_path}")}')
        tr.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            'Reused, TLSv1.3', 'The second connection should resume the session')

    def _add_reload_run(self, description: str) -> None:
        '''
        Request a configuration reload.

        This branch has no Test.AddConfigReload(). Waiting for the reload to take effect is left to
        the next run, which hangs a When.FileContains() readiness condition off a process start,
        the idiom the other reload tests on this branch use.

        :param description: Description for the reload test run.
        '''
        tr = Test.AddTestRun(description)
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Processes.Default.Command = 'traffic_ctl config reload'
        # traffic_ctl reads the environment to find the management socket.
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.ReturnCode = 0

    def _add_replace_bundle_run(self) -> None:
        '''Replace the global CA bundle in place with an unrelated CA and reload.'''
        tr = Test.AddTestRun('Replace the global client CA bundle in place')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Processes.Default.Command = f'cp ./bbb-ca.pem {self.bundle_path}'
        tr.Processes.Default.ReturnCode = 0

        # The SSL_CTX loads the bundle when it is built, so the replacement takes
        # effect through a reload rather than on the next connection. The bundle
        # is tracked like the certificate files, so the reload rebuilds the
        # certificate contexts without any other file having changed.
        self._add_reload_run('Reload to rebuild the SSL_CTX with the new bundle')

    def _add_stale_session_run(self) -> None:
        '''
        Offer the session verified under the replaced bundle.

        The session must not be resumed. The full handshake that follows presents
        the aaa-ca signed certificate again, which the new bundle does not trust,
        so no request is served.
        '''
        tr = Test.AddTestRun('A session verified by the replaced CA bundle does not resume')
        # Starting this origin is how the run waits for the reload to rebuild the certificate
        # contexts: two loads of ssl_multicert.config, the one at startup and the one just
        # requested.
        tr.Processes.Default.StartBefore(
            self.reload_server, ready=When.FileContains(self.ts.Disk.diags_log.Name, 'ssl_multicert.config finished loading', 2))
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = self._client_command(f'-sess_in {self.session_path}')
        # The two Excludes below would also hold if the client never reached ATS, which matters
        # because this runs right after a reload. These two prove it did: a negotiated full
        # handshake, then ATS refusing the certificate for the expected reason.
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            'New, TLSv1.3', 'The client must reach ATS and negotiate a full handshake')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'tlsv1 alert unknown ca', 'ATS must refuse the certificate the replaced bundle no longer trusts')
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            'Reused, TLSv1.3', 'A session verified by a CA that is no longer trusted must not resume')
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
        Assert that only the fresh and the legitimately resumed request reached the origin.

        A stale session that wrongly resumed would add a third access log line.
        '''
        tr = Test.AddTestRun('Wait for the access log')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Processes.Default.Command = (
            f"{os.path.join(Test.Variables.AtsTestToolsDir, 'condwait')} 60 3 -f "
            f"{os.path.join(self.ts.Variables.LOGDIR, 'squid.log')}")
        tr.Processes.Default.ReturnCode = 0

        self.ts.Disk.squid_log.Content = Testers.Lambda(
            lambda info, tester: TlsResumeCaBundleReload.check_regex_count(
                self.ts.Disk.squid_log.AbsPath, rf'^200 {re.escape(self._sni)}$', 2,
                'Only the fresh request and the resumption before the CA change should be served'))


TlsResumeCaBundleReload()
