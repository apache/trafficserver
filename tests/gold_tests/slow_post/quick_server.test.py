"""Verify ATS handles a server that replies before receiving a full request."""

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

from ports import get_port
import sys

Test.Summary = __doc__


class QuickServerTest:
    """Verify that ATS doesn't delay responses behind slow posts."""

    _init_file = '__init__.py'
    _http_utils = 'http_utils.py'
    _slow_post_client = 'slow_post_client.py'
    _quick_server = 'quick_server.py'

    _dns_counter = 0
    _server_counter = 0
    _ts_counter = 0

    def __init__(self, abort_request: bool, drain_request: bool, abort_response_headers: bool):
        """Initialize the test.

        :param drain_request: Whether the server should drain the request body.
        :param abort_request: Whether the client should abort the request body.
        before disconnecting.
        """
        self._should_drain_request = drain_request
        self._should_abort_request = abort_request
        self._should_abort_response_headers = abort_response_headers

    def _configure_dns(self, tr: 'TestRun') -> None:
        """Configure the DNS.

        :param tr: The test run to associate with the DNS process with.
        """
        self._dns = tr.MakeDNServer(f'dns-{QuickServerTest._dns_counter}', default='127.0.0.1')
        QuickServerTest._dns_counter += 1

    def _configure_server(self, tr: 'TestRun'):
        """Configure the origin server.

        This server replies with a response immediately after receiving the
        request headers.

        :param tr: The test run to associate with the server process with.
        """
        server = tr.Processes.Process(f'server-{QuickServerTest._server_counter}')
        QuickServerTest._server_counter += 1
        port = get_port(server, "http_port")
        server.Command = \
            f'{sys.executable} {self._quick_server} 127.0.0.1 {port} '
        if self._should_drain_request:
            server.Command += '--drain-request '
        if self._should_abort_response_headers:
            server.Command += '--abort-response-headers '
        server.Ready = When.PortOpenv4(port)
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure ATS.

        :param tr: The test run to associate with the ATS process with.
        """
        self._ts = tr.MakeATSProcess(f'ts-{QuickServerTest._ts_counter}')
        QuickServerTest._ts_counter += 1
        self._ts.Disk.remap_config.AddLine(f'map / http://quick.server.com:{self._server.Variables.http_port}')
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|dns|hostdb',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
            })

    def run(self):
        """Run the test."""
        tr = Test.AddTestRun()

        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Setup.CopyAs(self._init_file, Test.RunDirectory)
        tr.Setup.CopyAs(self._http_utils, Test.RunDirectory)
        tr.Setup.CopyAs(self._slow_post_client, Test.RunDirectory)
        tr.Setup.CopyAs(self._quick_server, Test.RunDirectory)

        client_command = (f'{sys.executable} {self._slow_post_client} '
                          '127.0.0.1 '
                          f'{self._ts.Variables.port} ')
        if not self._should_abort_request:
            client_command += '--finish-request '
        tr.Processes.Default.Command = client_command

        tr.Processes.Default.ReturnCode = 0
        self._ts.StartBefore(self._dns)
        self._ts.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Timeout = 10


for abort_request in [True, False]:
    for drain_request in [True, False]:
        for abort_response_headers in [True, False]:
            test = QuickServerTest(abort_request, drain_request, abort_response_headers)
            test.run()
