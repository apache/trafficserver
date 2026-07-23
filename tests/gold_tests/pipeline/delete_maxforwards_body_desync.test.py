'''Verify a DELETE self-answered from cache drains its request body.'''

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

Test.Summary = '''
Verify that a DELETE (Max-Forwards: 0) answered from the cache drains its request
body instead of leaving it to be parsed as the next request on the keep-alive
connection.
'''

Test.ContinueOnFail = False


class TestDeleteBodyDrain:
    """A DELETE with Max-Forwards: 0 is answered directly from cache. If the
    request body is not drained, its bytes are parsed as the next request on the
    keep-alive connection (a CL.0 desync). This drives that path and asserts the
    smuggled request never reaches the origin nor produces a second response."""

    _server_script: str = 'desync_server.py'
    _client_script: str = 'desync_client.py'
    _hostname: str = 'www.example.com'

    def __init__(self) -> None:
        tr = Test.AddTestRun('DELETE self-response must drain the request body.')
        tr.TimeOut = 60
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        self._configure_client(tr)

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        server = tr.Processes.Process('server')
        tr.Setup.Copy(self._server_script)
        port = get_port(server, 'http_port')
        server.Command = f'{sys.executable} {self._server_script} 127.0.0.1 {port}'
        server.Ready = When.PortOpenv4(port)
        # The origin must serve the warmed path, and must never see the smuggled one.
        server.Streams.All += Testers.ContainsExpression(r'ORIGIN_RECV path=\[/\]', 'origin should serve the warmed GET /')
        server.Streams.All += Testers.ExcludesExpression('poisoned', 'the smuggled request must never reach the origin')
        self._server = server
        return server

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        ts = tr.MakeATSProcess('ts', enable_cache=True)
        self._ts = ts
        ts.Disk.remap_config.AddLine(f'map http://{self._hostname}/ http://127.0.0.1:{self._server.Variables.http_port}/')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 0,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.http.cache.http': 1,
                'proxy.config.http.insert_age_in_response': 1,
            })
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        client = tr.Processes.Default
        tr.Setup.Copy(self._client_script)
        client.Command = f'{sys.executable} {self._client_script} 127.0.0.1 {self._ts.Variables.port} {self._hostname}'
        client.ReturnCode = 0
        # The DELETE must self-answer 200 from a warm cache, or the hit path was
        # never exercised and the test would pass vacuously.
        client.Streams.All += Testers.ContainsExpression('DELETE_STATUS=200', 'the DELETE must self-answer 200 from a warm cache')
        # The desync signatures: a second (smuggled) response, or smuggled bytes.
        client.Streams.All += Testers.ExcludesExpression(
            'SECOND_RESPONSE_RECEIVED=True', 'the client must not receive a smuggled second response')
        client.Streams.All += Testers.ExcludesExpression('poisoned', 'no smuggled response bytes should reach the client')
        client.StartBefore(self._server)
        client.StartBefore(self._ts)


TestDeleteBodyDrain()
