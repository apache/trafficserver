'''Non-idempotency audit: a NOOP self-response drains its body exactly once and keeps the connection alive.'''

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

Test.Summary = 'A cache-miss/NOOP DELETE self-response drains its body once and preserves keep-alive for the next request.'
Test.ContinueOnFail = False


class TestNoopKeepAlive:
    """Audit that the NOOP self-response drain runs exactly once.

    On one keep-alive connection, a NOOP self-response (a DELETE to an uncached
    path) with a benign body must drain that body exactly once; a following GET
    must then still be served. A double-drain would consume into the next request
    and close the connection.
    """

    _server_script: str = 'desync_server.py'
    _client_script: str = 'noop_keepalive_client.py'
    _hostname: str = 'www.example.com'

    def __init__(self) -> None:
        tr = Test.AddTestRun('A NOOP self-response must drain the body exactly once (keep-alive preserved).')
        tr.TimeOut = 40
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        self._configure_client(tr)

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        """Configure the origin server.

        :param tr: The test run to associate the origin server with.
        :return: The origin server process.
        """
        server = tr.Processes.Process('server')
        tr.Setup.Copy(self._server_script)
        port = get_port(server, 'http_port')
        server.Command = f'{sys.executable} {self._server_script} 127.0.0.1 {port}'
        server.Ready = When.PortOpenv4(port)
        self._server = server
        return server

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        """Configure ATS.

        :param tr: The test run to associate the ATS process with.
        :return: The ATS process.
        """
        ts = tr.MakeATSProcess('ts', enable_cache=True)
        self._ts = ts
        ts.Disk.remap_config.AddLine(f'map http://{self._hostname}/ http://127.0.0.1:{self._server.Variables.http_port}/')
        ts.Disk.records_config.update({'proxy.config.http.cache.http': 1})
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        """Configure the client.

        :param tr: The test run to associate the client process with.
        :return: The client process.
        """
        client = tr.Processes.Default
        tr.Setup.Copy(self._client_script)
        client.Command = f'{sys.executable} {self._client_script} 127.0.0.1 {self._ts.Variables.port} {self._hostname}'
        client.ReturnCode = 0
        client.Streams.All += Testers.ContainsExpression(
            'DELETE_STATUS=404', 'the DELETE to an uncached path must self-answer 404 (NOOP path)')
        client.Streams.All += Testers.ContainsExpression(
            'SECOND_REQUEST_STATUS=200', 'the following GET / must be served (keep-alive preserved, drained exactly once)')
        client.Streams.All += Testers.ContainsExpression(
            'KEEPALIVE_PRESERVED=yes', 'the connection must not be closed by a spurious double-drain')
        client.StartBefore(self._server)
        client.StartBefore(self._ts)


TestNoopKeepAlive()
