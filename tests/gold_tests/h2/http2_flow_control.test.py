"""Verify HTTP/2 flow control behavior."""

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
from typing import List, Optional


Test.Summary = __doc__


class Http2FlowControlTest:
    """Define an object to test HTTP/2 flow control behavior."""

    _replay_file: str = 'http2_flow_control.replay.yaml'
    _valid_policy_values: List[int] = list(range(0, 2))

    _default_initial_window_size: int = 65535
    _default_max_concurrent_streams_in: int = 100

    _dns_counter: int = 0
    _server_counter: int = 0
    _ts_counter: int = 0
    _client_counter: int = 0

    def __init__(
            self,
            description: str,
            initial_window_size: Optional[int] = None,
            max_concurrent_streams_in: Optional[int] = None,
            verify_window_update_frames: bool = False):
        """Declare the various test Processes.

        :param description: A description of the test.

        :param initial_window_size: The value with which to configure the
        proxy.config.http2.initial_window_size_in ATS parameter in the
        records.config file. If the paramenter is None, then no window size
        will be explicitly set and ATS will use the default value.

        :param max_concurrent_streams_in: The value with which to configure the
        proxy.config.http2.max_concurrent_streams_in ATS parameter in the
        records.config file. If the paramenter is None, then no window size
        will be explicitly set and ATS will use the default value.

        :param verify_window_update_frames: If True, then the test will verify
        that it sees HTTP/2 WINDOW_UPDATE frames as data is sent.
        """
        self._description = description

        self._initial_window_size = initial_window_size
        self._expected_initial_window_size = (
            initial_window_size if initial_window_size is not None
            else self._default_initial_window_size)

        self._max_concurrent_streams_in = max_concurrent_streams_in
        self._expected_max_concurrent_streams_in = (
            max_concurrent_streams_in if max_concurrent_streams_in is not None
            else self._default_max_concurrent_streams_in)

        self._verify_window_update_frames = verify_window_update_frames

        self._dns = self._configure_dns()
        self._server = self._configure_server()
        self._ts = self._configure_trafficserver()

    def _configure_dns(self):
        """Configure the DNS."""
        dns = Test.MakeDNServer(f'dns-{Http2FlowControlTest._dns_counter}')
        Http2FlowControlTest._dns_counter += 1
        return dns

    def _configure_server(self):
        """Configure the test server."""
        server = Test.MakeVerifierServerProcess(
            f'server-{Http2FlowControlTest._server_counter}',
            self._replay_file)
        Http2FlowControlTest._server_counter += 1
        return server

    def _configure_trafficserver(self):
        """Configure a Traffic Server process."""
        ts = Test.MakeATSProcess(
            f'ts-{Http2FlowControlTest._ts_counter}',
            enable_tls=True,
            enable_cache=False)
        Http2FlowControlTest._ts_counter += 1

        ts.addDefaultSSLFiles()
        ts.Disk.records_config.update({
            'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
            'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
            'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(self._dns.Variables.Port),

            'proxy.config.diags.debug.enabled': 3,
            'proxy.config.diags.debug.tags': 'http',
        })

        if self._initial_window_size is not None:
            ts.Disk.records_config.update({
                'proxy.config.http2.initial_window_size_in': self._initial_window_size,
            })

        if self._max_concurrent_streams_in is not None:
            ts.Disk.records_config.update({
                'proxy.config.http2.max_concurrent_streams_in': self._max_concurrent_streams_in,
            })

        ts.Disk.ssl_multicert_config.AddLine(
            'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
        )

        ts.Disk.remap_config.AddLine(
            f'map / http://127.0.0.1:{self._server.Variables.http_port}'
        )

        return ts

    def _configure_client(self, tr):
        """Configure a client process.

        :param tr: The TestRun to associate the client with.
        """
        tr.AddVerifierClientProcess(
            f'client-{Http2FlowControlTest._client_counter}',
            self._replay_file,
            https_ports=[self._ts.Variables.ssl_port])
        Http2FlowControlTest._client_counter += 1

        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            f'MAX_CONCURRENT_STREAMS:{self._expected_max_concurrent_streams_in}',
            "Client should receive a MAX_CONCURRENT_STREAMS setting.")

        if self._initial_window_size is not None:
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                f'INITIAL_WINDOW_SIZE:{self._expected_initial_window_size}',
                "Client should receive an INITIAL_WINDOW_SIZE setting.")

        if self._verify_window_update_frames:
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                f'WINDOW_UPDATE.*id 0: {self._expected_initial_window_size}',
                "Client should receive a session WINDOW_UPDATE.")

            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                f'WINDOW_UPDATE.*id 3: {self._expected_initial_window_size}',
                "Client should receive a stream WINDOW_UPDATE.")

            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                f'WINDOW_UPDATE.*id 5: {self._expected_initial_window_size}',
                "Client should receive a stream WINDOW_UPDATE.")

            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                f'WINDOW_UPDATE.*id 7: {self._expected_initial_window_size}',
                "Client should receive a stream WINDOW_UPDATE.")

    def run(self):
        """Configure the TestRun."""
        tr = Test.AddTestRun(self._description)
        self._configure_client(tr)
        tr.Processes.Default.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._server)
        tr.StillRunningAfter = self._ts
        tr.Processes.Default.StartBefore(self._ts)


#
# Default configuration.
#
test = Http2FlowControlTest("Default Configurations")
test.run()

#
# Configuring max_concurrent_streams_in.
#
test = Http2FlowControlTest(
    description="Configure max_concurrent_streams_in",
    max_concurrent_streams_in=53)
test.run()

#
# Configuring initial_window_size.
#
test = Http2FlowControlTest(
    description="Configure a large initial_window_size_in",
    initial_window_size=100123)
test.run()


test = Http2FlowControlTest(
    description="Configure a small initial_window_size_in",
    max_concurrent_streams_in=10,
    initial_window_size=10,
    verify_window_update_frames=True)
test.run()
