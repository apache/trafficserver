'''
Exercise fixed and dynamic TLS record sizing. On a large TLS download the body
must arrive intact and application-data records on the wire must follow the
configured sizing strategy.
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

Test.Summary = __doc__


class TestRecordSize:
    '''Verify fixed and dynamic TLS record sizing on large downloads.'''

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self, max_record: int, body_len: int) -> None:
        '''Declare the test Processes.'''
        self._max_record = max_record
        self._body_len = body_len
        self._server = self._configure_server()
        self._ts = self._configure_trafficserver()

    def _configure_server(self) -> 'Process':
        '''Configure the origin server with a large response body.

        :return: The origin server Process.
        '''
        server = Test.MakeOriginServer(f'server-{TestRecordSize._server_counter}')
        TestRecordSize._server_counter += 1

        body = "x" * self._body_len
        request_header = {"headers": "GET /obj HTTP/1.1\r\nHost: ex.test\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {
            "headers":
                "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                f"Cache-Control: max-age=3600\r\nContent-Length: {self._body_len}\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": body
        }
        server.addResponse("sessionlog.json", request_header, response_header)
        return server

    def _configure_trafficserver(self) -> 'Process':
        '''Configure Traffic Server with the requested record-size strategy.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestRecordSize._ts_counter}', enable_tls=True)
        TestRecordSize._ts_counter += 1

        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.max_record_size': self._max_record,
            })
        if self._max_record == -1:
            ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
                r'proxy\.config\.ssl\.max_record_size.*Validity Check error',
                'The dynamic record-size sentinel should pass records validation')
        return ts

    def run(self) -> None:
        '''Configure and run the TestRun.

        The client downloads the object and measures the TLS records on the wire.
        '''
        if self._max_record == -1:
            description = 'max_record_size=-1 dynamically sizes records on a large TLS download'
            client_option = '--dynamic'
            expected_output = 'PASS: TLS records ramp from small to large after the dynamic threshold'
        else:
            description = 'max_record_size>0 clamps records on a large TLS download'
            client_option = f'--max-record {self._max_record}'
            expected_output = 'PASS: every application-data record is within the configured clamp'

        tr = Test.AddTestRun(description)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = (
            f'{sys.executable} {os.path.join(Test.TestDirectory, "tls_record_size_client.py")} '
            f'-p {self._ts.Variables.ssl_port} --host ex.test --path /obj '
            f'{client_option} --expect-bytes {self._body_len}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            expected_output, 'TLS records must follow the configured sizing strategy')
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server


# The fixed-size test response is comfortably larger than its 4,096-byte clamp,
# ensuring that many records exercise the clamp. The dynamic-sizing test
# response must exceed its 1,000,000-byte threshold by enough data to demonstrate
# both phases.
TestRecordSize(4096, 1024 * 1024).run()
TestRecordSize(-1, 2 * 1024 * 1024).run()
