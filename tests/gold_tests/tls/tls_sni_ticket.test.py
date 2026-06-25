'''
Test sni.yaml session ticket overrides.
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
from typing import Any

Test.Summary = '''
Test sni.yaml session ticket overrides
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))
Test.Setup.Copy('file.ticket')


class TlsSniTicketTest:
    _server_is_started = False
    _ts_on_started = False
    _ts_off_started = False

    def __init__(self) -> None:
        """
        Initialize shared test state and configure the ATS processes.
        """
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self.setupOriginServer()
        self.setupEnabledTS()
        self.setupDisabledTS()

    def setupOriginServer(self) -> None:
        """
        Configure the origin server with a simple response for all requests.
        """
        request_header = {
            'headers': 'GET / HTTP/1.1\r\nHost: tickets.example.com\r\n\r\n',
            'timestamp': '1469733493.993',
            'body': ''
        }
        response_header = {
            'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
            'timestamp': '1469733493.993',
            'body': 'ticket test'
        }
        self.server = Test.MakeOriginServer('server')
        self.server.addResponse('sessionlog.json', request_header, response_header)

    def setupTS(
            self,
            name: str,
            sni_name: str,
            global_ticket_enabled: int,
            global_ticket_number: int,
            sni_ticket_enabled: int,
            sni_ticket_number: int | None = None) -> Any:
        """
        Configure an ATS process for one SNI ticket override scenario.

        :param name: ATS process name.
        :param sni_name: SNI hostname matched in sni.yaml.
        :param global_ticket_enabled: Process-wide session ticket enable setting.
        :param global_ticket_number: Process-wide TLSv1.3 ticket count.
        :param sni_ticket_enabled: Per-SNI session ticket enable override.
        :param sni_ticket_number: Per-SNI TLSv1.3 ticket count override.
        :return: Configured ATS process.
        """
        ts = Test.MakeATSProcess(name, enable_tls=True)

        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.Port}')
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.ssl.session_cache.mode': 0,
                'proxy.config.ssl.server.session_ticket.enable': global_ticket_enabled,
                'proxy.config.ssl.server.session_ticket.number': global_ticket_number,
                'proxy.config.ssl.server.ticket_key.filename': self.ticket_file,
            })

        sni_lines = [
            'sni:',
            f'- fqdn: {sni_name}',
            f'  ssl_ticket_enabled: {sni_ticket_enabled}',
        ]
        if sni_ticket_number is not None:
            sni_lines.append(f'  ssl_ticket_number: {sni_ticket_number}')
        ts.Disk.sni_yaml.AddLines(sni_lines)

        return ts

    def setupEnabledTS(self) -> None:
        """
        Create the ATS process whose SNI rule enables tickets.
        """
        self.ts_on = self.setupTS('ts_on', 'tickets-on.com', 0, 0, 1, 3)

    def setupDisabledTS(self) -> None:
        """
        Create the ATS process whose SNI rule disables tickets.
        """
        self.ts_off = self.setupTS('ts_off', 'tickets-off.com', 1, 2, 0)

    def start_processes_if_needed(
            self, tr: Any, start_server: bool = False, start_ts_on: bool = False, start_ts_off: bool = False) -> None:
        """
        Register one-time StartBefore hooks for the processes needed by a test run.

        :param tr: The AuTest run definition being configured.
        :param start_server: Whether the origin server should be started for this run.
        :param start_ts_on: Whether the tickets-enabled ATS process should be started for this run.
        :param start_ts_off: Whether the tickets-disabled ATS process should be started for this run.
        """
        if start_server and not TlsSniTicketTest._server_is_started:
            tr.Processes.Default.StartBefore(self.server)
            TlsSniTicketTest._server_is_started = True

        if start_ts_on and not TlsSniTicketTest._ts_on_started:
            tr.Processes.Default.StartBefore(self.ts_on)
            TlsSniTicketTest._ts_on_started = True

        if start_ts_off and not TlsSniTicketTest._ts_off_started:
            tr.Processes.Default.StartBefore(self.ts_off)
            TlsSniTicketTest._ts_off_started = True

    @staticmethod
    def check_regex_count(output_path: str, pattern: str, expected_count: int, description: str) -> tuple[bool, str, str]:
        """
        Count regex matches in a process output file.

        :param output_path: Path to the output file to inspect.
        :param pattern: Regex pattern to count.
        :param expected_count: Expected number of matches.
        :param description: Description reported by the tester.
        :return: AuTest lambda result tuple.
        """
        with open(output_path, 'r') as f:
            content = f.read()

        matches = re.findall(pattern, content)
        if len(matches) == expected_count:
            return (True, description, f'Found {len(matches)} matches for {pattern}')
        return (False, description, f'Expected {expected_count} matches for {pattern}, found {len(matches)}')

    @staticmethod
    def session_reuse_command(port: int, servername: str) -> str:
        """
        Build a TLSv1.2 resumption command for a specific SNI name.

        :param port: ATS TLS listening port.
        :param servername: SNI hostname to send with the connection.
        :return: Shell command for repeated TLSv1.2 session reuse attempts.
        """
        return (
            f'session_path=`mktemp` && '
            f'echo -e "GET / HTTP/1.1\\r\\nHost: {servername}\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {servername} -sess_out "$$session_path" -tls1_2 && '
            f'echo -e "GET / HTTP/1.1\\r\\nHost: {servername}\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {servername} -sess_in "$$session_path" -tls1_2 && '
            f'echo -e "GET / HTTP/1.1\\r\\nHost: {servername}\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {servername} -sess_in "$$session_path" -tls1_2 && '
            f'echo -e "GET / HTTP/1.1\\r\\nHost: {servername}\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {servername} -sess_in "$$session_path" -tls1_2 && '
            f'echo -e "GET / HTTP/1.1\\r\\nHost: {servername}\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {servername} -sess_in "$$session_path" -tls1_2 && '
            f'echo -e "GET / HTTP/1.1\\r\\nHost: {servername}\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{port} -servername {servername} -sess_in "$$session_path" -tls1_2')

    def add_tls12_enabled_run(self) -> None:
        """
        Register the TLSv1.2 resumption test for the enabled SNI case.
        """
        tr = Test.AddTestRun('sni.yaml enables TLSv1.2 ticket resumption')
        tr.Command = TlsSniTicketTest.session_reuse_command(self.ts_on.Variables.ssl_port, 'tickets-on.com')
        tr.ReturnCode = 0
        self.start_processes_if_needed(tr, start_server=True, start_ts_on=True)
        tr.Processes.Default.Streams.All.Content = Testers.Lambda(
            lambda info, tester: TlsSniTicketTest.check_regex_count(
                tr.Processes.Default.Streams.All.AbsPath, r'Reused, TLSv1\.2', 5,
                'Check that tickets-on.com reuses TLSv1.2 sessions'))
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts_on

    def add_tls13_enabled_run(self) -> None:
        """
        Register the TLSv1.3 ticket count test for the enabled SNI case.
        """
        tr = Test.AddTestRun('sni.yaml sets TLSv1.3 ticket count')
        tr.Command = (
            f'echo -e "GET / HTTP/1.1\\r\\nHost: tickets-on.com\\r\\nConnection: close\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{self.ts_on.Variables.ssl_port} -servername tickets-on.com -tls1_3 -msg -ign_eof')
        tr.ReturnCode = 0
        self.start_processes_if_needed(tr, start_server=True, start_ts_on=True)
        tr.Processes.Default.Streams.All.Content = Testers.Lambda(
            lambda info, tester: TlsSniTicketTest.check_regex_count(
                tr.Processes.Default.Streams.All.AbsPath, r'NewSessionTicket', 3,
                'Check that tickets-on.com receives three TLSv1.3 tickets'))
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts_on

    def add_tls12_disabled_run(self) -> None:
        """
        Register the TLSv1.2 non-resumption test for the disabled SNI case.
        """
        tr = Test.AddTestRun('sni.yaml disables TLSv1.2 ticket resumption')
        tr.Command = TlsSniTicketTest.session_reuse_command(self.ts_off.Variables.ssl_port, 'tickets-off.com')
        self.start_processes_if_needed(tr, start_server=True, start_ts_off=True)
        tr.Processes.Default.Streams.All = Testers.ExcludesExpression('Reused', 'tickets-off.com should not reuse TLSv1.2 sessions')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression('TLSv1.2', 'tickets-off.com should still negotiate TLSv1.2')
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts_off

    def add_tls13_disabled_run(self) -> None:
        """
        Register the TLSv1.3 no-ticket test for the disabled SNI case.
        """
        tr = Test.AddTestRun('sni.yaml disables TLSv1.3 ticket issuance')
        tr.Command = (
            f'echo -e "GET / HTTP/1.1\\r\\nHost: tickets-off.com\\r\\nConnection: close\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{self.ts_off.Variables.ssl_port} -servername tickets-off.com -tls1_3 -msg -ign_eof'
        )
        self.start_processes_if_needed(tr, start_server=True, start_ts_off=True)
        tr.Processes.Default.Streams.All.Content = Testers.Lambda(
            lambda info, tester: TlsSniTicketTest.check_regex_count(
                tr.Processes.Default.Streams.All.AbsPath, r'NewSessionTicket', 0,
                'Check that tickets-off.com receives no TLSv1.3 tickets'))
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += self.ts_off

    def run(self) -> None:
        """
        Register all AuTest runs for the SNI ticket override coverage.
        """
        self.add_tls12_enabled_run()
        self.add_tls13_enabled_run()
        self.add_tls12_disabled_run()
        self.add_tls13_disabled_run()


TlsSniTicketTest().run()
