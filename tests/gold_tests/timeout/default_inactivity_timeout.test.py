'''
Verify correct behavior for default_inactivity_timeout.
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

Test.Summary = __doc__


class TestDefaultInactivityTimeout:
    """Configure a test for default_inactivity_test."""

    replay_file = "replay/default_inactivity_timeout.replay.yaml"
    client_gold_file = 'gold/client_default_inactivity_timeout.gold'
    client_counter: int = 0
    ts_counter: int = 0
    server_counter: int = 0

    def __init__(self, name: str, use_override: bool):
        """Initialize the test.

        :param name: The name of the test.
        :param use_override: Whether to use the override rather than the global
        configuration.
        """
        self.name = name
        self.use_override = use_override

    def _configure_server(self, tr: 'TestRun') -> None:
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        server = tr.AddVerifierServerProcess(
            f"server_{TestDefaultInactivityTimeout.server_counter}",
            self.replay_file)
        TestDefaultInactivityTimeout.server_counter += 1
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun') -> None:
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        :return: A Traffic Server Test process.
        """
        ts = tr.MakeATSProcess(f"ts-{TestDefaultInactivityTimeout.ts_counter}")
        TestDefaultInactivityTimeout.ts_counter += 1
        self._ts = ts

        debug_tags = 'http|cache|socket|net_queue|inactivity_cop|conf_remap'
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': debug_tags,
        })

        origin_port = self._server.Variables.http_port
        remap_line = f'map / http://127.0.0.1:{origin_port}'

        if self.use_override:
            remap_line += (
                ' @plugin=conf_remap.so '
                '@pparam=proxy.config.net.default_inactivity_timeout=2')
        else:
            ts.Disk.records_config.update({
                'proxy.config.net.default_inactivity_timeout': 2,
            })

        ts.Disk.remap_config.AddLine(remap_line)
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'timed out due to default inactivity timeout',
            'Verify that the default inactivity timeout was triggered.')

    def run(self) -> None:
        """Run the test."""
        tr = Test.AddTestRun(self.name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.AddVerifierClientProcess(
            f'client-{TestDefaultInactivityTimeout.client_counter}',
            self.replay_file,
            http_ports=[self._ts.Variables.port])
        TestDefaultInactivityTimeout.client_counter += 1

        # Set up expectectations for the timeout closing the connection.
        tr.Processes.Default.ReturnCode = 1
        tr.Processes.Default.Streams.all = self.client_gold_file


test = TestDefaultInactivityTimeout("global config", use_override=False)
test.run()

test = TestDefaultInactivityTimeout("override config", use_override=True)
test.run()
