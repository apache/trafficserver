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

Test.Summary = 'Basic checks on QUIC max_idle_timeout set by ts.quic.no_activity_timeout_in'

Test.SkipUnless(Condition.HasATSFeature('TS_HAS_QUICHE'), Condition.HasCurlFeature('http3'))


class Test_quic_no_activity_timeout:
    """Configure a test for QUIC no_activity_timeout_in."""

    replay_file = "replay/quic_no_activity_timeout.replay.yaml"
    client_counter: int = 0
    ts_counter: int = 0
    server_counter: int = 0

    def __init__(self, name: str, no_activity_timeout_in=1000, gold_file="", replay_keys="", extra_recs=None):
        """Initialize the test.

        :param name: The name of the test.
        :param no_activity_timeout_in: Configuration value for ts.quic.no_activity_timeout_in
        :param gold_file: Gold file to be checked.
        :param replay_keys: Keys to be used by pv
        :param extra_recs: Any additional records to be set, either a yaml string or a dict.
        """
        self.name = name
        self.no_activity_timeout_in = no_activity_timeout_in
        self.gold_file = gold_file
        self.replay_keys = replay_keys
        self.extra_recs = extra_recs

    def _configure_server(self, tr: 'TestRun'):
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        server = tr.AddVerifierServerProcess(f"server_{Test_quic_no_activity_timeout.server_counter}", self.replay_file)
        Test_quic_no_activity_timeout.server_counter += 1
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(f"ts-{Test_quic_no_activity_timeout.ts_counter}", enable_quic=True, enable_tls=True)

        Test_quic_no_activity_timeout.ts_counter += 1
        self._ts = ts
        self._ts.addSSLfile("ssl/cert.crt")
        self._ts.addSSLfile("ssl/private-key.key")
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl|net|v_quic|quic|http|socket|inactivity_cop',
                'proxy.config.quic.no_activity_timeout_in': self.no_activity_timeout_in,
                'proxy.config.quic.qlog.file_base': f'log/qlog_{Test_quic_no_activity_timeout.server_counter}',
            })

        if self.extra_recs:
            self._ts.Disk.records_config.update(self.extra_recs)

        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')
        self._ts.Disk.ssl_multicert_config.AddLine(
            f'dest_ip=* ssl_cert_name={self._ts.Variables.SSLDir}/cert.crt ssl_key_name={self._ts.Variables.SSLDir}/private-key.key'
        )

    def run(self, check_for_max_idle_timeout=False):
        """Run the test."""
        tr = Test.AddTestRun(self.name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess(
            f'client-{Test_quic_no_activity_timeout.client_counter}',
            self.replay_file,
            http3_ports=[self._ts.Variables.ssl_port],
            keys=self.replay_keys)
        Test_quic_no_activity_timeout.client_counter += 1

        if check_for_max_idle_timeout:
            tr.Processes.Default.ReturnCode = 1  # timeout
            self._ts.Disk.traffic_out.Content += Testers.IncludesExpression(
                "QUIC Idle timeout detected", "We should detect the timeout.")
        else:
            tr.Processes.Default.ReturnCode = 0

        if self.gold_file:
            tr.Processes.Default.Streams.all = self.gold_file


# Tests start.

test0 = Test_quic_no_activity_timeout(
    "Test ts.quic.no_activity_timeout_in(quic max_idle_timeout), no delays",
    no_activity_timeout_in=0,  # no timeout `max_idle_timeout`
    replay_keys="nodelays")
test0.run()

test1 = Test_quic_no_activity_timeout(
    "Test ts.quic.no_activity_timeout_in(quic max_idle_timeout) with a 5s delay",
    no_activity_timeout_in=3000,  # 3s `max_idle_timeout`
    replay_keys="delay5s",
    gold_file="gold/quic_no_activity_timeout.gold")
test1.run(check_for_max_idle_timeout=True)

# QUIC Ignores the default_inactivity_timeout config, so the ts.quic.no_activity_timeout_in
# should be honor
test2 = Test_quic_no_activity_timeout(
    "Ignoring default_inactivity_timeout and use the ts.quic.no_activity_timeout_in instead",
    replay_keys="delay5s",
    no_activity_timeout_in=3000,
    extra_recs={'proxy.config.net.default_inactivity_timeout': 1})
test2.run(check_for_max_idle_timeout=True)
