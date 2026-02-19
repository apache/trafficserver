"""Verify the proxy.process.http.000_responses stat is incremented for client aborts."""

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


class MetricResponse000Test:
    """Verify that the 000_responses stat is incremented when a client aborts."""

    _abort_client = 'abort_client.py'
    _server_counter = 0
    _ts_counter = 0

    def __init__(self):
        """Configure and run the test."""
        self._configure_server()
        self._configure_traffic_server()
        self._configure_abort_client()
        self._configure_successful_request()
        self._verify_000_metric()

    def _configure_server(self) -> None:
        """Configure the origin server."""
        self._server = Test.MakeOriginServer(f'server-{MetricResponse000Test._server_counter}')
        MetricResponse000Test._server_counter += 1

        request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        self._server.addResponse("sessionlog.json", request_header, response_header)

    def _configure_traffic_server(self) -> None:
        """Configure ATS."""
        self._ts = Test.MakeATSProcess(f'ts-{MetricResponse000Test._ts_counter}', enable_cache=False)
        MetricResponse000Test._ts_counter += 1

        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}/')
        self._ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 0,
            'proxy.config.diags.debug.tags': 'http',
        })

    def _configure_abort_client(self) -> None:
        """Configure a client to send a partial request and abort."""
        tr = Test.AddTestRun('Trigger a client abort with partial request')

        tr.Setup.CopyAs(os.path.join(Test.TestDirectory, self._abort_client), Test.RunDirectory)

        p = tr.Processes.Default
        p.Command = f'{sys.executable} {self._abort_client} 127.0.0.1 {self._ts.Variables.port}'
        p.ReturnCode = 0

        self._ts.StartBefore(self._server)
        p.StartBefore(self._ts)

        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server

    def _configure_successful_request(self) -> None:
        """Send a successful request to verify it doesn't increment 000 stat."""
        tr = Test.AddTestRun('Send a successful request')
        tr.Processes.Default.Command = f'curl -s -o /dev/null -w "%{{http_code}}" http://127.0.0.1:{self._ts.Variables.port}/'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression('200', 'Expected 200 response')
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server

    def _verify_000_metric(self) -> None:
        """Verify the 000_responses stat is incremented."""
        # Wait for stats to propagate.
        tr = Test.AddTestRun('Wait for stats')
        tr.Processes.Default.Command = 'sleep 2'
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts

        # Verify the 000_responses stat is non-zero.
        tr = Test.AddTestRun('Check 000_responses stat')
        tr.Processes.Default.Command = 'traffic_ctl metric get proxy.process.http.000_responses'
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            'proxy.process.http.000_responses 1', 'The 000_responses stat should be 1')
        tr.StillRunningAfter = self._ts


MetricResponse000Test()
