'''
Test that a TLS session is not resumed against a different server certificate when no SNI is sent.
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
Test that a session issued on one dest_ip-selected certificate is not resumed
on a different one. Such connections send no SNI, so the server name in the
session id context cannot distinguish them.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TlsResumeCertPartition:
    '''
    Test that resumption is partitioned by server certificate for no-SNI connections.

    A client that sends no SNI has its certificate chosen by destination
    address, so the server name cannot tell two such connections apart. This
    asserts the end-to-end behavior rather than any one mechanism: today the
    partition comes from each dest_ip entry getting its own session ticket
    keyblock, not from the session id context, so the test passes with or
    without a certificate term in that context. It does fail if that keyblock
    becomes shared, which is what keeps it from being vacuous.

    The second address is the IPv6 loopback because it needs no interface
    alias on any platform, unlike 127.0.0.2.
    '''

    _first_ip = '127.0.0.1'
    _second_ip = '[::1]'

    def __init__(self) -> None:
        '''Configure the origin server, ATS process, and test runs.'''
        Test.Setup.Copy('file.ticket')
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self._configure_server()
        self._configure_ts()
        self._add_same_cert_run()
        self._add_cross_cert_run()

    def _configure_server(self) -> None:
        '''Configure the origin server with a simple response.'''
        server = Test.MakeOriginServer('server')
        request_header = {
            'headers': 'GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n',
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
        '''Configure the ATS process with a different certificate per destination address.'''
        ts = Test.MakeATSProcess('ts', enable_tls=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.addSSLfile('ssl/signed-foo.pem')
        ts.addSSLfile('ssl/signed-foo.key')

        # Two certificates on one listener, chosen by destination address rather
        # than by SNI. Neither entry sets ssl_ca_name, so the only thing that
        # differs between the two connections is which certificate is served.
        ts.Disk.ssl_multicert_config.AddLines(
            [
                f'dest_ip={self._first_ip} ssl_cert_name=server.pem ssl_key_name=server.key',
                f'dest_ip={self._second_ip} ssl_cert_name=signed-foo.pem ssl_key_name=signed-foo.key',
                'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key',
            ])

        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.Port}')

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.ssl.server.session_ticket.enable': 1,
                'proxy.config.ssl.server.ticket_key.filename': self.ticket_file,
            })
        self.ts = ts

    def _client_command(self, ip: str, session_arg: str) -> str:
        '''
        Build a shell command that connects to the given address without sending an SNI.

        :param ip: Destination address to connect to, which selects the certificate.
        :param session_arg: The s_client argument saving or offering a session.
        :return: Shell command performing the connection.
        '''
        # The IPv6 loopback is on lo0 everywhere, whereas 127.0.0.2 needs an alias on macOS.
        port = self.ts.Variables.ssl_portv6 if ip.startswith('[') else self.ts.Variables.ssl_port
        request = 'printf "GET / HTTP/1.1\\r\\nHost: example.com\\r\\nConnection: close\\r\\n\\r\\n"'
        # No -servername, so no SNI extension is sent and dest_ip selects the cert.
        return (f'{request} | openssl s_client -connect {ip}:{port} -noservername '
                f'{session_arg} -tls1_3 -ign_eof')

    def _add_same_cert_run(self) -> None:
        '''Add a control run proving a no-SNI session resumes against its own certificate.'''
        path = os.path.join(Test.RunDirectory, 'session-same')
        tr = Test.AddTestRun('A no-SNI session resumes on the certificate that issued it')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = (
            f'{self._client_command(self._first_ip, f"-sess_out {path}")} && '
            f'{self._client_command(self._first_ip, f"-sess_in {path}")}')
        tr.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            'Reused, TLSv1.3', 'A no-SNI session should resume against the same certificate')

    def _add_cross_cert_run(self) -> None:
        '''Add a run offering a no-SNI session against a different certificate.'''
        path = os.path.join(Test.RunDirectory, 'session-cross')
        tr = Test.AddTestRun('A no-SNI session does not resume on a different certificate')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts
        tr.Command = (
            f'{self._client_command(self._first_ip, f"-sess_out {path}")} && '
            f'{self._client_command(self._second_ip, f"-sess_in {path}")}')
        tr.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ExcludesExpression(
            'Reused', 'A session must not resume against a certificate other than the one that issued it')
        # Without these the run also passes when the second entry never matches and both legs are
        # served the same certificate from the wildcard entry, which proves nothing.
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'CN=random.server.com', 'The first leg must be served the first certificate')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'CN=foo.com', 'The second leg must be served the second certificate')


TlsResumeCertPartition()
