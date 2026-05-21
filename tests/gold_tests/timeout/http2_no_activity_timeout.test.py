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

from typing import Optional
import re

Test.Summary = 'Verify http2.no_activity_timeout_(in|out)'


class Test_http2_no_activity_timeout:
    """Configure a test for http2 no_activity_timeout_(in|out)."""

    replay_file = "replay/http2_no_activity_timeout.replay.yaml"
    client_counter: int = 0
    ts_counter: int = 0
    server_counter: int = 0

    def __init__(
            self,
            name: str,
            replay_keys: Optional[str] = None,
            no_activity_timeout_in: Optional[int] = None,
            expect_in_timeout=False,
            no_activity_timeout_out: Optional[int] = None,
            expect_out_timeout=False):
        """Initialize the test.

        :param name: The name of the test.
        :param replay_keys: The set of keys to replay from the replay file. If
          not provided, all keys will be replayed.
        :param no_activity_timeout_in: Configuration value for
          ts.http2.no_activity_timeout_in. None means that the default value will
          be used.
        :param expect_in_timeout: True if there is an expected inbound timeout.
        :param no_activity_timeout_out: Configuration value for
          ts.http2.no_activity_timeout_out. None means that the default value will
          be used.
        :param expect_out_timeout: True if there is an expected outbound timeout.
        """
        self._name = name
        self._replay_keys = replay_keys
        self._no_activity_timeout_in = no_activity_timeout_in
        self._expect_in_timeout = expect_in_timeout
        self._no_activity_timeout_out = no_activity_timeout_out
        self._expect_out_timeout = expect_out_timeout

    def _configure_server(self, tr: 'TestRun'):
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        self._server = tr.AddVerifierServerProcess(f"server_{Test_http2_no_activity_timeout.server_counter}", self.replay_file)
        Test_http2_no_activity_timeout.server_counter += 1

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        self._ts = tr.MakeATSProcess(f"ts-{Test_http2_no_activity_timeout.ts_counter}", enable_tls=True, enable_cache=False)
        Test_http2_no_activity_timeout.ts_counter += 1

        self._ts.addSSLfile("ssl/cert.crt")
        self._ts.addSSLfile("ssl/private-key.key")
        self._ts.Disk.ssl_multicert_config.AddLine(
            f'dest_ip=* ssl_cert_name={self._ts.Variables.SSLDir}/cert.crt '
            f'ssl_key_name={self._ts.Variables.SSLDir}/private-key.key')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|socket|inactivity_cop',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
                "proxy.config.ssl.client.verify.server.policy": 'PERMISSIVE',
                'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
            })

        if self._no_activity_timeout_in is not None:
            self._ts.Disk.records_config.update({
                'proxy.config.http2.no_activity_timeout_in': self._no_activity_timeout_in,
            })
        if self._no_activity_timeout_out is not None:
            self._ts.Disk.records_config.update({
                'proxy.config.http2.no_activity_timeout_out': self._no_activity_timeout_out,
            })

        self._ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{self._server.Variables.https_port}')

    def run(self):
        """Run the test."""
        tr = Test.AddTestRun(self._name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess(
            f'client-{Test_http2_no_activity_timeout.client_counter}',
            self.replay_file,
            https_ports=[self._ts.Variables.ssl_port],
            keys=self._replay_keys)
        Test_http2_no_activity_timeout.client_counter += 1

        if self._expect_in_timeout:
            tr.Processes.Default.Streams.All += Testers.IncludesExpression(
                "SSL_read error", "The client should have a read error due to an ATS timeout.")
            self._ts.Disk.traffic_out.Content += Testers.IncludesExpression(
                "http2_cs.*Closing event:.*TIMEOUT", "We should detect a client side timeout.")
        elif self._expect_out_timeout:
            # There should be two origin connections:
            # 1. For the first no delay transaction.
            # 2. Another after a timeout, closing the first.
            self._server.Streams.All += Testers.IncludesExpression(
                "Negotiated ALPN from client ALPN.*Negotiated ALPN from client ALPN",
                "A second server side connection should be needed after the first times out.",
                reflags=re.MULTILINE | re.DOTALL)
            self._ts.Disk.traffic_out.Content += Testers.IncludesExpression(
                "http2_cs.*Closing event:.*TIMEOUT", "We should detect a server side timeout.")


test0 = Test_http2_no_activity_timeout("Default no activity timeout", expect_in_timeout=False, expect_out_timeout=False)
test0.run()

test1 = Test_http2_no_activity_timeout(
    "Client (in) side inactivity timeout",
    no_activity_timeout_in=1,
    replay_keys="no_delay 3_second_request_delay",
    expect_in_timeout=True,
    expect_out_timeout=False)
test1.run()

test2 = Test_http2_no_activity_timeout(
    "Server (out) side inactivity timeout",
    no_activity_timeout_out=1,
    replay_keys="no_delay 3_second_request_delay",
    expect_in_timeout=False,
    expect_out_timeout=True)
test2.run()
