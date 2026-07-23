'''Verify the cache-miss (INTERNAL_CACHE_NOOP) DELETE self-response drains its body.'''

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

Test.Summary = 'A DELETE to an uncached path self-answers 404 (NOOP) and must drain its body.'
Test.ContinueOnFail = False


class TestDeleteMaxfwdNoopDrain:
    """A DELETE to an uncached path is self-answered 404 via INTERNAL_CACHE_NOOP.

    Verify the accompanying request body is drained so its bytes are not framed as
    the next request on the connection.
    """

    _server_script: str = 'desync_server.py'
    _client_script: str = 'desync_client_miss.py'
    _hostname: str = 'www.example.com'

    def __init__(self) -> None:
        tr = Test.AddTestRun('A NOOP-path DELETE self-response must drain its request body.')
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
        server.Streams.All += Testers.ExcludesExpression('misspoison', 'the smuggled request must not reach the origin')
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
            'DELETE_STATUS=404', 'the DELETE must hit the NOOP (miss) path, not a cache hit')
        client.Streams.All += Testers.ExcludesExpression('SECOND_RESPONSE_RECEIVED=True', 'no smuggled second response')
        client.Streams.All += Testers.ExcludesExpression('misspoison', 'no smuggled bytes should reach the client')
        client.StartBefore(self._server)
        client.StartBefore(self._ts)


TestDeleteMaxfwdNoopDrain()
