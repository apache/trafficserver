'''
Exercise the TLS record-size clamp (proxy.config.ssl.max_record_size > 0): on a
large TLS download the body must arrive intact and every application-data record
on the wire must be clamped to the configured size.
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

# NOTE: only the positive (fixed-clamp) branch of the record-sizing logic is
# covered here. The documented dynamic mode (max_record_size == -1) cannot be
# enabled through records.yaml because the record's validity check is [0-16383],
# which rejects -1; that inconsistency is pre-existing, so the dynamic branch
# stays uncovered by design.

import os
import sys

Test.Summary = __doc__


class TestRecordSizeClamp:
    '''Verify max_record_size clamps every record of a large TLS download.'''

    # Comfortably larger than the clamp so many records pass through it.
    _body_len: int = 1024 * 1024
    _max_record: int = 4096

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        self._server = self._configure_server()
        self._ts = self._configure_trafficserver()

    def _configure_server(self) -> 'Process':
        '''Configure the origin server with a large response body.

        :return: The origin server Process.
        '''
        server = Test.MakeOriginServer(f'server-{TestRecordSizeClamp._server_counter}')
        TestRecordSizeClamp._server_counter += 1

        body = "x" * TestRecordSizeClamp._body_len
        request_header = {"headers": "GET /obj HTTP/1.1\r\nHost: ex.test\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {
            "headers":
                "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                f"Cache-Control: max-age=3600\r\nContent-Length: {TestRecordSizeClamp._body_len}\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": body
        }
        server.addResponse("sessionlog.json", request_header, response_header)
        return server

    def _configure_trafficserver(self) -> 'Process':
        '''Configure Traffic Server with a positive max_record_size clamp.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestRecordSizeClamp._ts_counter}', enable_tls=True)
        TestRecordSizeClamp._ts_counter += 1

        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                # Positive cap -> the write path clamps each TLS record to this many bytes.
                'proxy.config.ssl.max_record_size': TestRecordSizeClamp._max_record,
            })
        return ts

    def run(self) -> None:
        '''Configure and run the TestRun.

        The client downloads the object and measures the TLS records on the wire,
        asserting both that the body is intact and that no application-data record
        exceeds the configured clamp.
        '''
        tr = Test.AddTestRun("max_record_size>0 clamps records on a large TLS download")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = (
            f'{sys.executable} {os.path.join(Test.TestDirectory, "tls_record_size_client.py")} '
            f'-p {self._ts.Variables.ssl_port} --host ex.test --path /obj '
            f'--max-record {TestRecordSizeClamp._max_record} --expect-bytes {TestRecordSizeClamp._body_len}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            "PASS: every application-data record is within the configured clamp",
            "every TLS record must be clamped to the configured size")
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server


TestRecordSizeClamp().run()
