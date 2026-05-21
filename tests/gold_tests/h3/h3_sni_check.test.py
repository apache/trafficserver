'''
Verify h3 SNI checking behavior.
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

Test.Summary = '''
Verify h3 SNI checking behavior.
'''

Test.SkipUnless(Condition.HasATSFeature('TS_HAS_QUICHE'), Condition.HasCurlFeature('http3'))

Test.ContinueOnFail = True


class Test_sni_check:
    """Configure a test to verify SNI checking behavior for h3 connections."""

    replay_file = "replays/h3_sni.replay.yaml"
    client_counter: int = 0
    ts_counter: int = 0
    server_counter: int = 0

    def __init__(self, name: str, gold_file="", replay_keys="", expect_request_rejected=False):
        """Initialize the test.

        :param name: The name of the test.
        :param gold_file: (Optional) Gold file to be checked.
        :param replay_keys: (Optional) Keys to be used by pv.
        :param expect_request_rejected: (Optional) Whether or not the client request is expected to be rejected.
        """
        self.name = name
        self.gold_file = gold_file
        self.replay_keys = replay_keys
        self.expect_request_rejected = expect_request_rejected

    def _configure_server(self, tr: 'TestRun'):
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        server = tr.AddVerifierServerProcess(f"server_{Test_sni_check.server_counter}", self.replay_file)
        Test_sni_check.server_counter += 1
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(f"ts-{Test_sni_check.ts_counter}", enable_quic=True, enable_tls=True)

        Test_sni_check.ts_counter += 1
        self._ts = ts
        # Configure TLS for Traffic Server.
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
                'proxy.config.quic.no_activity_timeout_in': 0,
                'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
            })

        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')

    def run(self):
        """Run the test."""
        tr = Test.AddTestRun(self.name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess(
            f'client-{Test_sni_check.client_counter}',
            self.replay_file,
            http3_ports=[self._ts.Variables.ssl_port],
            keys=self.replay_keys)
        Test_sni_check.client_counter += 1

        if self.expect_request_rejected:
            # The client request should time out because ATS rejects it and does
            # not send a response.
            tr.Processes.Default.ReturnCode = 1
            self._ts.Disk.traffic_out.Content += Testers.ContainsExpression("SNI not found", "ATS should detect the missing SNI.")
        else:
            # Verify the client request is successful.
            tr.Processes.Default.ReturnCode = 0
            self._ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
                "SNI not found", "ATS should see the SNI presented by client.")

        if self.gold_file:
            tr.Processes.Default.Streams.all = self.gold_file


# TEST 1: Client request with SNI.
test0 = Test_sni_check("", replay_keys="has_sni", expect_request_rejected=False)
test0.run()

# TEST 2: Client request without SNI.
test1 = Test_sni_check("", replay_keys="no_sni", expect_request_rejected=True)
test1.run()
