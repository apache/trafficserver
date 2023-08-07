'''
Verify ip_allow filtering behavior.
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
Verify ip_allow filtering behavior.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_HAS_QUICHE'),
    Condition.HasCurlFeature('http3')
)
Test.ContinueOnFail = True


class Test_quic:
    """Configure a test for running quic traffic."""

    replay_file = "replays/h3.replay.yaml"
    client_counter: int = 0
    ts_counter: int = 0
    server_counter: int = 0

    def __init__(self, name: str, gold_file="", replay_keys=""):
        """Initialize the test.

        :param name: The name of the test.
        :param gold_file: Gold file to be checked.
        :param replay_keys: Keys to be used by pv
        """
        self.name = name
        self.gold_file = gold_file
        self.replay_keys = replay_keys

    def _configure_server(self, tr: 'TestRun'):
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        server = tr.AddVerifierServerProcess(
            f"server_{Test_quic.server_counter}",
            self.replay_file)
        Test_quic.server_counter += 1
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(f"ts-{Test_quic.ts_counter}", enable_quic=True, enable_tls=True)

        Test_quic.ts_counter += 1
        self._ts = ts
        # Configure TLS for Traffic Server.
        self._ts.addDefaultSSLFiles()

        self._ts.Disk.ssl_multicert_config.AddLine(
            'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
        )
        self._ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl|net|v_quic|quic|http|socket|ip_allow',
            'proxy.config.quic.qlog.file_base': f'log/qlog_{Test_quic.server_counter}',
            'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.quic.no_activity_timeout_in': 0,
            'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        })

        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')
        # ip_allow policy: Deny all methods from all IP.
        self._ts.Disk.ip_allow_yaml.AddLines(
            '''ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: deny
    methods: ALL
'''.split("\n")
        )

    def run(self):
        """Run the test."""
        tr = Test.AddTestRun(self.name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess(
            f'client-{Test_quic.client_counter}',
            self.replay_file,
            http3_ports=[self._ts.Variables.ssl_port],
            keys=self.replay_keys)
        Test_quic.client_counter += 1

        tr.Processes.Default.ReturnCode = 0

        if self.gold_file:
            tr.Processes.Default.Streams.all = self.gold_file


#
# TEST 1: Perform a GET request. Should be denied in accept().
#

test0 = Test_quic(
    "Basic test")
test0.run()
