'''Test a pipelined requests.'''

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

Test.Summary = '''Test pipelined requests and HTTP/1.1 request framing.'''

IP_ALLOW_CONTENT = '''
ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: deny
    methods:
      - DELETE
'''


class TestPipelining:
    """Verify that a set of pipelined requests is handled correctly."""

    _client_script: str = 'pipeline_client.py'
    _server_script: str = 'pipeline_server.py'

    _dns_counter: int = 0
    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self, buffer_requests: bool) -> None:
        """Configure the TestRun.

        :param buffer_requests: Whether to configure ATS to buffer client requests.
        """
        tr = Test.AddTestRun('Test a pipelined chunked encoded request.')
        tr.TimeOut = 10
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_traffic_server(tr, buffer_requests)
        self._configure_client(tr)

    def _configure_dns(self, tr: 'TestRun') -> 'Process':
        """Configure the DNS.

        :param tr: The test run to associate with the DNS process with.
        :return: The DNS process.
        """
        name = f'dns_{self._dns_counter}'
        TestPipelining._dns_counter += 1
        dns = tr.MakeDNServer(name, default='127.0.0.1')
        self._dns = dns
        return dns

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        """Configure the origin server.

        :param tr: The test run to associate with the origin server with.
        :return: The origin server process.
        """
        name = f'server_{self._server_counter}'
        TestPipelining._server_counter += 1
        server = tr.Processes.Process(name)
        tr.Setup.Copy(self._server_script)
        port = get_port(server, "http_port")
        server.Command = f'{sys.executable} {self._server_script} 127.0.0.1 {port} '
        server.ReturnCode = 0
        server.Ready = When.PortOpenv4(port)
        server.Streams.All += Testers.ContainsExpression('/first', 'Should receive the first request')
        server.Streams.All += Testers.ContainsExpression('/second', 'Should receive the second request')

        # The third request should be denied due to the ip_allow.yaml rule.
        server.Streams.All += Testers.ExcludesExpression('/third', 'Should not receive the third request')
        self._server = server
        return server

    def _configure_traffic_server(self, tr: 'TestRun', buffer_requests: bool) -> 'Process':
        """Configure ATS.

        :param tr: The test run to associate with the ATS process with.
        :param buffer_requests: Whether to enable request_buffer_enabled.
        :return: The ATS process.
        """
        name = f'ts_{self._ts_counter}'
        TestPipelining._ts_counter += 1
        ts = tr.MakeATSProcess(name, enable_cache=False)
        self._ts = ts
        ts.Disk.remap_config.AddLine(f'map / http://backend.server.com:{self._server.Variables.http_port}')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|ip_allow',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
            })
        if buffer_requests:
            ts.Disk.records_config.update({
                'proxy.config.http.request_buffer_enabled': 1,
            })
        ts.Disk.ip_allow_yaml.AddLines(IP_ALLOW_CONTENT.split("\n"))
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        """Configure the client.

        :param tr: The test run to associate with the client process with.
        :return: The client process.
        """
        client = tr.Processes.Default
        tr.Setup.Copy(self._client_script)
        client.Command = (f'{sys.executable} {self._client_script} 127.0.0.1 {self._ts.Variables.port} '
                          'server.com server.com')
        client.ReturnCode = 0
        client.Streams.All += Testers.ContainsExpression('X-Response: first', "Should receive the origin's first response.")
        client.Streams.All += Testers.ContainsExpression('X-Response: second', "Should receive the origin's second response.")
        client.Streams.All += Testers.ExcludesExpression('X-Response: third', "Should not receive the origin's third response.")
        client.Streams.All += Testers.ContainsExpression('403', 'Should receive the ATS-generated rejection of the DELETE request.')
        client.StartBefore(self._dns)
        client.StartBefore(self._server)
        client.StartBefore(self._ts)


class TestRequestFraming:
    """Verify Traffic Server preserves HTTP/1.1 request framing.

    A body-less POST (Content-Length: 0) immediately followed by a second
    pipelined request must be delivered to the origin as two independent
    requests, so the second request cannot be folded into the first. A request
    with conflicting Content-Length header fields must be rejected rather than
    forwarded.
    """

    _client_script: str = 'request_framing_client.py'
    _server_script: str = 'request_framing_server.py'
    _counter: int = 0

    def __init__(self, mode: str) -> None:
        """Configure a test run for the given request mode.

        :param mode: 'pipeline' for a body-less POST followed by a pipelined
            request, or 'conflicting_cl' for a request with conflicting
            Content-Length header fields.
        """
        self._mode = mode
        self._name = f'framing_{mode}_{TestRequestFraming._counter}'
        TestRequestFraming._counter += 1

        description = {
            'pipeline': 'Test a body-less POST followed by a pipelined request.',
            'conflicting_cl': 'Test a request with conflicting Content-Length headers.',
        }[mode]
        tr = Test.AddTestRun(description)
        tr.TimeOut = 20
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        self._configure_client(tr)

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        """Configure the recording origin server."""
        server = tr.Processes.Process(f'origin_{self._name}')
        tr.Setup.Copy(self._server_script)
        http_port = get_port(server, "http_port")
        server.Command = f'{sys.executable} {self._server_script} 127.0.0.1 {http_port} '
        server.ReturnCode = 0
        server.Ready = When.PortOpenv4(http_port)

        if self._mode == 'pipeline':
            # The origin must see exactly the two requests the client sent, with
            # their boundaries intact. If ATS folded them together, the origin
            # would see a single request or the second request's bytes inside
            # the POST body.
            server.Streams.All += Testers.ContainsExpression(
                r'REQUEST_LINE: POST / HTTP/1.1', 'Origin should receive the POST as its own request.')
            server.Streams.All += Testers.ContainsExpression(
                r'REQUEST_LINE: GET /second HTTP/1.1', 'Origin should receive the GET as its own request.')
            server.Streams.All += Testers.ContainsExpression(
                r'ORIGIN_REQUEST_COUNT: 2', 'Origin should receive the second request as a distinct request.')
            server.Streams.All += Testers.ExcludesExpression(
                r'ORIGIN_REQUEST_COUNT: 3', 'Origin should receive exactly two requests, no more.')
            # The second request's bytes must never appear inside the first
            # (POST) request's body.
            server.Streams.All += Testers.ExcludesExpression(
                r"BODY:.*GET /second", 'The GET request must not appear inside the POST body.')
            server.Streams.All += Testers.ExcludesExpression(
                r"BODY:.*X-Marker", 'The second request header must not appear in the POST body.')
        else:
            # An ambiguously-framed request must be rejected by ATS before it
            # ever reaches the origin.
            server.Streams.All += Testers.ExcludesExpression(
                r'REQUEST_LINE:', 'Origin must not receive an ambiguously-framed request.')
        self._server = server
        return server

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        """Configure ATS as a reverse proxy in front of the origin."""
        ts = tr.MakeATSProcess(f'ts_{self._name}', enable_cache=False)
        self._ts = ts
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}/')
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http',
        })
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        """Configure the client that sends the framed request."""
        client = tr.Processes.Default
        tr.Setup.Copy(self._client_script)
        client.Command = (
            f'{sys.executable} {self._client_script} 127.0.0.1 {self._ts.Variables.port} '
            f'www.example.com {self._mode}')
        client.ReturnCode = 0
        if self._mode == 'pipeline':
            # Two independent responses must come back, one per request.
            client.Streams.All += Testers.ContainsExpression(
                r'STATUS_LINE_COUNT: 2', 'Client should receive two independent responses.')
            client.Streams.All += Testers.ContainsExpression(
                r'X-Origin-Response: first', 'Client should receive the response to the POST.')
            client.Streams.All += Testers.ContainsExpression(
                r'X-Origin-Response: second', 'Client should receive the response to the GET.')
        else:
            # The conflicting Content-Length request must be rejected with a
            # 400, exactly one response must come back, and the second request
            # must never be answered.
            client.Streams.All += Testers.ContainsExpression(
                r'HTTP/1.1 400', 'Client should receive a 400 for the ambiguous request.')
            client.Streams.All += Testers.ContainsExpression(r'STATUS_LINE_COUNT: 1', 'Client should receive exactly one response.')
            client.Streams.All += Testers.ExcludesExpression(
                r'X-Origin-Response: second', 'The second request must not be answered.')
        client.StartBefore(self._server)
        client.StartBefore(self._ts)


TestPipelining(buffer_requests=False)
TestPipelining(buffer_requests=True)

TestRequestFraming(mode='pipeline')
TestRequestFraming(mode='conflicting_cl')
