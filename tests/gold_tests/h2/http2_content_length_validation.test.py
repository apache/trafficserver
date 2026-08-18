'''
Verify HTTP/2 requests whose Content-Length disagrees with the DATA payload are rejected.
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
import sys

Test.Summary = 'HTTP/2 Content-Length is validated against the DATA payload at full width'
Test.ContinueOnFail = True


class Http2ContentLengthValidationTest:
    '''Verify the HTTP/2 Content-Length consistency check is not narrowed.'''

    client_script: str = 'clients/h2_content_length_client.py'

    # Every case sends a 3-byte DATA payload. The declared Content-Length of the
    # first two cases looks consistent with that payload only if it is truncated
    # to a uint32_t: 2^32 wraps to 0, which short-circuits the consistency check
    # entirely, and 2^32 + 3 wraps to the payload length itself.
    rejected_cases: tuple = (
        ('cl-wraps-to-zero', 'Content-Length of 2^32, which truncates to a uint32_t 0'),
        ('cl-wraps-to-payload-length', 'Content-Length of 2^32 + 3, which truncates to a uint32_t 3'),
        ('cl-larger-than-payload', 'Content-Length larger than the DATA payload'),
        ('cl-negative', 'negative Content-Length'),
    )

    def __init__(self) -> None:
        '''Configure the processes shared by every test run.'''
        self._configure_server()
        self._configure_traffic_server()
        self._configure_client()

    def _configure_server(self) -> 'Process':
        '''Configure the origin server process.

        :return: The origin server process.
        '''
        server = Test.MakeOriginServer('server')
        server.addResponse(
            'sessionlog.json', {
                'headers': 'POST /cl-matches HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 3\r\n\r\n',
                'timestamp': '1469733493.993',
                'body': 'abc',
            }, {
                'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n',
                'timestamp': '1469733493.993',
                'body': '',
            })
        self._server = server
        return server

    def _configure_traffic_server(self) -> 'Process':
        '''Configure the Traffic Server process.

        :return: The Traffic Server process.
        '''
        ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split('\n'))
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http2',
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}/')

        # The warning reports the values that were compared. A truncated
        # comparison could not report a Content-Length of 2^32 alongside a
        # payload length of 3.
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Bad payload length content_length=4294967296 data_length=3',
            'ATS should compare the Content-Length and the payload length at full width.',
        )
        self._ts = ts
        return ts

    def _configure_client(self) -> None:
        '''Copy the client script into the run directory.'''
        self._client = os.path.basename(self.client_script)
        Test.Setup.CopyAs(self.client_script, Test.RunDirectory)

    def _add_test_run(self, scenario: str, description: str, start_processes: bool) -> 'TestRun':
        '''Add a test run driving a single Content-Length scenario.

        :param scenario: The client scenario to run.
        :param description: A human readable description of the scenario.
        :param start_processes: Whether this run should start the shared processes.
        :return: The added TestRun.
        '''
        tr = Test.AddTestRun(description)
        tr.Processes.Default.Command = f'{sys.executable} {self._client} {self._ts.Variables.ssl_port} {scenario}'
        tr.Processes.Default.ReturnCode = 0
        if start_processes:
            tr.Processes.Default.StartBefore(self._server)
            tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter += self._server
        return tr

    def run(self) -> None:
        '''Drive each Content-Length scenario against ATS.'''
        for index, (scenario, description) in enumerate(self.rejected_cases):
            tr = self._add_test_run(scenario, f'HTTP/2 POST with a {description} is rejected', index == 0)
            # The client exits non-zero unless the rejection carries a
            # PROTOCOL_ERROR or a 4xx status, so the return code above pins down
            # how ATS rejected this.
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                r'Received (RST_STREAM|GOAWAY|HTTP/2 response with status 4\d\d)',
                f'ATS should reject an HTTP/2 POST with a {description}.',
            )

        tr = self._add_test_run('cl-matches', 'HTTP/2 POST with a Content-Length matching the DATA payload is proxied', False)
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'Received HTTP/2 response with status 200',
            'A consistent Content-Length should still be proxied to the origin.',
        )


Http2ContentLengthValidationTest().run()
