'''
Test that TLS session tickets are partitioned by the certificate selected by destination address.
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
import sys

Test.Summary = '''
Test that a session ticket issued on one dest_ip-selected certificate is not
resumed on a different one, while servers sharing the ticket keys and the
certificate still resume each other's tickets.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))


class TlsResumeCertPartition:
    '''
    Test that ticket resumption is partitioned by the certificate selected by destination address.

    A client that sends no SNI has its certificate chosen by destination
    address, so the server name cannot tell two such connections apart.
    Session tickets for every certificate are protected by the same globally
    configured ticket keys, so a ticket carries nothing tying it to the
    certificate that issued it unless the keys used for it depend on that
    certificate. Without that, a ticket issued on one address resumes on another
    address serving a different certificate, and the client skips the
    certificate it would otherwise have been shown.

    Each run checks what the client observes, which certificate each leg is
    served and whether it resumed, and the log checks that ATS records the
    resumption the same way. The second address is the IPv6 loopback because it
    needs no interface alias on any platform, unlike 127.0.0.2. For the PROXY
    protocol runs the destination comes from the PROXY header, so documentation
    addresses stand in for load balancer VIPs, one IPv4 pair and one IPv6 pair.
    No request is routed to an origin: ATS answers each one itself, and only the
    TLS handshake and the resumption it logs matter here.
    '''

    _first_ip = '127.0.0.1'
    _second_ip = '::1'
    _first_vip = '192.0.2.1'
    _second_vip = '192.0.2.2'
    _first_vip6 = '2001:db8::1'
    _second_vip6 = '2001:db8::2'
    _first_cn = 'random.server.com'
    _second_cn = 'foo.com'
    _client = 'tls_resume_cert_partition_client.py'

    def __init__(self) -> None:
        '''Configure the ATS processes and test runs.'''
        Test.Setup.Copy('file.ticket')
        Test.Setup.Copy(self._client)
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self.ts = self._configure_ts('ts')
        # A second instance sharing the ticket key file stands in for another server in a fleet.
        self.ts2 = self._configure_ts('ts2')
        self._started = False
        self._second_legs: list[tuple['Process', str, bool]] = []

        for tls in ('1.3', '1.2'):
            self._add_run(
                f'A no-SNI TLS {tls} session resumes on the certificate that issued it',
                tls,
                self._leg(self.ts, self._first_ip, f'/same-{tls}-first'),
                self._leg(self.ts, self._first_ip, f'/same-{tls}-second'),
                resumed=True,
                cns=(self._first_cn, self._first_cn))
            self._add_run(
                f'A no-SNI TLS {tls} session does not resume on a different certificate',
                tls,
                self._leg(self.ts, self._first_ip, f'/cross-{tls}-first'),
                self._leg(self.ts, self._second_ip, f'/cross-{tls}-second'),
                resumed=False,
                cns=(self._first_cn, self._second_cn))
        self._add_run(
            'A PROXY protocol session resumes on the VIP whose certificate issued it',
            '1.3',
            self._leg(self.ts, self._first_ip, '/proxy-same-first', self._first_vip),
            self._leg(self.ts, self._first_ip, '/proxy-same-second', self._first_vip),
            resumed=True,
            cns=(self._first_cn, self._first_cn))
        self._add_run(
            'A PROXY protocol session does not resume on a VIP with a different certificate',
            '1.3',
            self._leg(self.ts, self._first_ip, '/proxy-cross-first', self._first_vip),
            self._leg(self.ts, self._first_ip, '/proxy-cross-second', self._second_vip),
            resumed=False,
            cns=(self._first_cn, self._second_cn))
        self._add_run(
            'An IPv6 PROXY protocol session does not resume on a VIP with a different certificate',
            '1.3',
            self._leg(self.ts, self._first_ip, '/proxy6-cross-first', self._first_vip6),
            self._leg(self.ts, self._first_ip, '/proxy6-cross-second', self._second_vip6),
            resumed=False,
            cns=(self._first_cn, self._second_cn))
        self._add_run(
            'A no-SNI session resumes on another server sharing the ticket keys and certificate',
            '1.3',
            self._leg(self.ts, self._first_ip, '/shared-first'),
            self._leg(self.ts2, self._first_ip, '/shared-second'),
            resumed=True,
            cns=(self._first_cn, self._first_cn))
        self._add_log_checks()

    def _configure_ts(self, name: str) -> 'Process':
        '''
        Configure an ATS process with a different certificate per destination address.

        :param name: Name of the ATS process.
        :return: The configured ATS process.
        '''
        ts = Test.MakeATSProcess(name, enable_tls=True, enable_proxy_protocol=True)
        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.addSSLfile('ssl/signed-foo.pem')
        ts.addSSLfile('ssl/signed-foo.key')
        # Only for the client, which verifies so that it can report which certificate it was served.
        ts.addSSLfile('ssl/signer.pem')

        # Certificates chosen by destination address rather than by SNI. Neither
        # certificate is chosen by name, so the only thing that differs between two
        # connections is which certificate is served.
        ts.Disk.ssl_multicert_yaml.AddLines(
            f"""
ssl_multicert:
  - dest_ip: "{self._first_ip}"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
  - dest_ip: "[{self._second_ip}]"
    ssl_cert_name: signed-foo.pem
    ssl_key_name: signed-foo.key
  - dest_ip: "{self._first_vip}"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
  - dest_ip: "{self._second_vip}"
    ssl_cert_name: signed-foo.pem
    ssl_key_name: signed-foo.key
  - dest_ip: "[{self._first_vip6}]"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
  - dest_ip: "[{self._second_vip6}]"
    ssl_cert_name: signed-foo.pem
    ssl_key_name: signed-foo.key
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.ssl.server.session_ticket.enable': 1,
                'proxy.config.ssl.server.ticket_key.filename': self.ticket_file,
                'proxy.config.http.proxy_protocol_allowlist': '127.0.0.1,::1',
            })

        ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: resumption
      format: '%<cqup> %<cqssr> %<cqssrt>'
  logs:
    - mode: ascii
      format: resumption
      filename: resumption
'''.split("\n"))
        return ts

    def _leg(self, ts: 'Process', ip: str, path: str, proxy_dst: str | None = None) -> tuple['Process', str, str]:
        '''
        Describe one client connection.

        :param ts: The ATS process to connect to.
        :param ip: The address to connect to, which selects the certificate unless a PROXY header is sent.
        :param path: The request path, which identifies the connection in the log.
        :param proxy_dst: If given, the destination to name in a PROXY header.
        :return: The ATS process, the request path, and the leg's client arguments, with the role left as a
            ``{role}`` placeholder.
        '''
        if proxy_dst is not None:
            port = ts.Variables.proxy_protocol_ssl_port
        else:
            port = ts.Variables.ssl_portv6 if ':' in ip else ts.Variables.ssl_port
        return ts, path, f'{ip} {port} {path}' + (f' {{role}}-proxy-dst {proxy_dst}' if proxy_dst is not None else '')

    def _add_run(
            self, name: str, tls: str, first: tuple['Process', str, str], second: tuple['Process', str, str], resumed: bool,
            cns: tuple[str, str]) -> None:
        '''
        Add a run that saves a session on one connection and offers it on another.

        :param name: The name of the run.
        :param tls: The TLS version to use.
        :param first: The connection that saves the session.
        :param second: The connection that offers it.
        :param resumed: Whether the second connection should resume.
        :param cns: The certificate CN each connection should be served.
        '''
        tr = Test.AddTestRun(name)
        if not self._started:
            tr.Processes.Default.StartBefore(self.ts)
            tr.Processes.Default.StartBefore(self.ts2)
            self._started = True
        tr.StillRunningAfter = [self.ts, self.ts2]
        cas = ' '.join(f'--ca {os.path.join(self.ts.Variables.SSLDir, ca)}' for ca in ('server.pem', 'signer.pem'))
        legs = ' '.join(
            f'--{role} ' + leg[2].replace('{role}', f'--{role}') for role, leg in (('first', first), ('second', second)))
        tr.Processes.Default.Command = f'{sys.executable} {self._client} {cas} --tls {tls} {legs}'
        tr.Processes.Default.ReturnCode = 0

        first_path, second_path = first[1], second[1]
        # Asserting the certificate each leg is served keeps a run from passing when both legs
        # happen to be served the same certificate, which would prove nothing.
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'{first_path}: CN={cns[0]} reused=False ', 'The first leg must do a full handshake')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'{second_path}: CN={cns[1]} reused={resumed} ',
            'The second leg must resume' if resumed else 'The second leg must not resume')

        self._second_legs.append((second[0], second_path, resumed))

    def _add_log_checks(self) -> None:
        '''Check that ATS records resumption for each second leg as the client saw it.'''
        for ts, path, resumed in self._second_legs:
            # cqssr is 1 and cqssrt 2 for a ticket resumption, both 0 for a full handshake.
            # cqup is logged without the leading slash.
            expected = f'{path.lstrip("/")} ' + ('1 2' if resumed else '0 0')
            log = os.path.join(ts.Variables.LOGDIR, 'resumption.log')
            name = 'ts2' if ts is self.ts2 else 'ts'
            tr = Test.AddAwaitFileContainsTestRun(f'{name} logs "{expected}"', log, re.escape(expected))
            tr.StillRunningAfter = [self.ts, self.ts2]


TlsResumeCertPartition()
