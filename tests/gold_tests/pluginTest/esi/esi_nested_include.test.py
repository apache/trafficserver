'''
Test nested include for the ESI plugin.
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
Test nested include for the ESI plugin.
'''

Test.SkipUnless(Condition.PluginExists('esi.so'),)


class EsiTest():
    """
    A class that encapsulates the configuration and execution of a set of ESI
    test cases.
    """

    _replay_file: str = "esi_nested_include.replay.yaml"

    def __init__(self, plugin_config) -> None:
        """
        :param plugin_config: esi.so configuration for plugin.config.
        """
        tr = Test.AddTestRun("Request the ESI generated document")
        self._create_server(tr)
        self._create_ats(tr, plugin_config)
        self._create_client(tr)

    def _create_server(self, tr: 'TestRun') -> 'Process':
        """ Create and start a server process.
        :param tr: The test run to add the server to.
        :return: The server process.
        """
        # Configure our server using proxy verifier.
        server = tr.AddVerifierServerProcess("server", self._replay_file, other_args='--format "{url}"')
        self._server = server

        # Validate server traffic
        server.Streams.All += Testers.ContainsExpression('GET /main.php', 'Verify the server received the initial request.')
        server.Streams.All += Testers.ContainsExpression(
            'GET /esi-nested-include.html', 'Verify the server received the nested include request.')

        return server

    def _create_ats(self, tr: 'TestRun', plugin_config: str) -> 'Process':
        """ Create and start an ATS process.
        :param tr: The test run to add the ATS to.
        :param plugin_config: The plugin configuration to use.
        :return: The ATS process.
        """
        # Configure ATS with a vanilla ESI plugin configuration.
        ts = tr.MakeATSProcess(f"ts")
        self._ts = ts
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|plugin_esi',
        })
        server_port = self._server.Variables.http_port
        ts.Disk.remap_config.AddLine(f'map http://www.example.com/ http://127.0.0.1:{server_port}')
        ts.Disk.plugin_config.AddLine(plugin_config)

        ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r'The current esi inclusion depth \(3\) is larger than or equal to the max \(3\)',
            'Verify the ESI error concerning the max inclusion depth')
        return ts

    def _create_client(self, tr: 'TestRun') -> None:
        """ Create and start a client process to generate the request.
        :param tr: The test run to add the client to.
        """
        # Note, just request the main.php file. Otherwise the client will do the
        # ESI requests in the replay file as well.
        p = tr.AddVerifierClientProcess(
            "client", self._replay_file, http_ports=[self._ts.Variables.port], other_args='--format "{url}" --keys /main.php')
        p.ReturnCode = 0
        p.StartBefore(self._server)
        p.StartBefore(self._ts)

        # Double check that the client received the response.
        p.Streams.stdout += Testers.ContainsExpression(
            'Received an HTTP/1 chunked body', 'Verify the client received the response.')

        p.Streams.stdout += Testers.ContainsExpression(
            'esi:include src="http://www.example.com/esi-nested-include.html"/>', 'Verify the ATS received the esi include.')


#
# Configure and run the test cases.
#
EsiTest(plugin_config='esi.so')
