'''
Test proxy.config.ssl.server.session_ticket.number.
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
Test proxy.config.ssl.server.session_ticket.number
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))
Test.Setup.Copy('file.ticket')


class TlsTicketNumberTest:
    '''Verify that the global record controls the TLSv1.3 ticket count.

    Two ATS processes issue a different, non-default number of tickets so that
    an implementation which ignores the record cannot satisfy both runs. No
    sni.yaml is configured, so the counts can only come from the record.
    '''

    _hostname = 'ticket-number.example.com'

    def __init__(self) -> None:
        '''Configure the origin server and both ATS processes.'''
        self.ticket_file = os.path.join(Test.RunDirectory, 'file.ticket')
        self.setupOriginServer()
        self.ts_three = self.setupTS('ts_three', 3)
        self.ts_one = self.setupTS('ts_one', 1)

    def setupOriginServer(self) -> None:
        '''Configure the origin server with a simple response for all requests.'''
        request_header = {'headers': f'GET / HTTP/1.1\r\nHost: {self._hostname}\r\n\r\n', 'timestamp': '1469733493.993', 'body': ''}
        response_header = {
            'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
            'timestamp': '1469733493.993',
            'body': 'ticket number test'
        }
        self.server = Test.MakeOriginServer('server')
        self.server.addResponse('sessionlog.json', request_header, response_header)

    def setupTS(self, name: str, ticket_number: int) -> Any:
        '''Configure an ATS process issuing a given number of TLSv1.3 tickets.

        :param name: ATS process name.
        :param ticket_number: Value for proxy.config.ssl.server.session_ticket.number.
        :return: Configured ATS process.
        '''
        ts = Test.MakeATSProcess(name, enable_tls=True)

        ts.addSSLfile('ssl/server.pem')
        ts.addSSLfile('ssl/server.key')
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self.server.Variables.Port}')
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl',
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.ssl.server.session_ticket.enable': 1,
                'proxy.config.ssl.server.session_ticket.number': ticket_number,
                'proxy.config.ssl.server.ticket_key.filename': self.ticket_file,
            })

        return ts

    @staticmethod
    def check_ticket_count(output_path: str, expected_count: int, description: str) -> tuple[bool, str, str]:
        '''Count the NewSessionTicket messages in an s_client trace.

        :param output_path: Path to the output file to inspect.
        :param expected_count: Number of tickets the server is expected to issue.
        :param description: Description reported by the tester.
        :return: AuTest lambda result tuple.
        '''
        with open(output_path, 'r') as f:
            content = f.read()

        matches = re.findall(r'NewSessionTicket', content)
        if len(matches) == expected_count:
            return (True, description, f'Received {len(matches)} tickets')
        return (False, description, f'Expected {expected_count} tickets, received {len(matches)}')

    def add_ticket_count_run(self, ts: Any, ticket_number: int, start_server: bool) -> None:
        '''Register a run asserting how many TLSv1.3 tickets one ATS process issues.

        :param ts: The ATS process to connect to.
        :param ticket_number: Number of tickets the process is configured to issue.
        :param start_server: Whether the origin server should be started for this run.
        '''
        tr = Test.AddTestRun(f'records.yaml sets the TLSv1.3 ticket count to {ticket_number}')
        tr.Command = (
            f'printf "GET / HTTP/1.1\\r\\nHost: {self._hostname}\\r\\nConnection: close\\r\\n\\r\\n" | '
            f'openssl s_client -connect 127.0.0.1:{ts.Variables.ssl_port} -tls1_3 -msg -ign_eof')
        tr.ReturnCode = 0
        if start_server:
            tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Streams.All.Content = Testers.Lambda(
            lambda info, tester: TlsTicketNumberTest.check_ticket_count(
                tr.Processes.Default.Streams.All.AbsPath, ticket_number,
                f'Check that the server issues {ticket_number} TLSv1.3 tickets'))
        tr.StillRunningAfter += self.server
        tr.StillRunningAfter += ts

    def run(self) -> None:
        '''Register all AuTest runs for the global ticket count record.'''
        self.add_ticket_count_run(self.ts_three, 3, start_server=True)
        self.add_ticket_count_run(self.ts_one, 1, start_server=False)


TlsTicketNumberTest().run()
