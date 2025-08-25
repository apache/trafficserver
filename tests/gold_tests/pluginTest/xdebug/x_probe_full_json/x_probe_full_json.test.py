'''
Verify xdebug plugin probe-full-json functionality.
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

Test.Summary = 'Test xdebug plugin probe-full-json functionality'
Test.ContinueOnFail = True
Test.SkipUnless(Condition.PluginExists('xdebug.so'))
Test.SkipUnless(Condition.HasProgram("jq", "jq is required to validate JSON output"))


class XDebugProbeFullJsonTest:
    """
    Test the xdebug probe-full-json functionality which transforms the response body
    to include request headers, response body, and response headers in a complete JSON format.

    The probe-full-json feature:
    - Changes Content-Type to text/plain
    - Generates a complete JSON object containing all debug information
    - Includes client/server request headers, response body, and client/server response headers
    - Disables caching due to body modification
    """

    _replay_file: str = "x_probe_full_json.replay.yaml"

    def __init__(self) -> None:
        self._servers_are_started: bool = False
        self._setupOriginServer()
        self._setupTS()
        self._setupClient()
        self._setupJqValidation()

    def _setupOriginServer(self) -> None:
        """Configure the origin server using Proxy Verifier.
        """
        self._server = Test.MakeVerifierServerProcess("server", self._replay_file)

    def _setupTS(self) -> None:
        """Configure ATS with xdebug plugin enabled for probe-full-json functionality.
        """
        self._ts = Test.MakeATSProcess("ts")

        self._ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "xdebug",
        })

        self._ts.Disk.plugin_config.AddLine('xdebug.so --enable=probe-full-json')
        self._ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.http_port}")

    def _startServersIfNeeded(self, tr: 'TestRun') -> None:
        '''Start the servers if they are not already started.
        :param tr: TestRun to add the test to.
        '''
        if not self._servers_are_started:
            tr.Processes.Default.StartBefore(self._server)
            tr.Processes.Default.StartBefore(self._ts)
            self._servers_are_started = True

    def _setupClient(self) -> None:
        """Test basic probe-full-json functionality with JSON output.
        """
        tr = Test.AddTestRun("Verify probe-full-json functionality")
        self._startServersIfNeeded(tr)
        tr.AddVerifierClientProcess("client", self._replay_file, http_ports=[self._ts.Variables.port])
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'X-Original-Content-Type', "X-Original-Content-Type should be present")

    def _setupJqValidation(self) -> None:
        """Use curl to get the response body and pipe through jq to validate JSON.
        """
        tr = Test.AddTestRun("Verify JSON output")
        self._startServersIfNeeded(tr)
        tr.MakeCurlCommand(
            f'-s -H"uuid: 1" -H "Host: example.com" -H "X-Debug: probe-full-json" '
            f'http://127.0.0.1:{self._ts.Variables.port}/test | '
            "jq '.\"client-request\".\"uuid\",.\"server-body\",.\"proxy-response\".\"x-response\"'")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/jq.gold"


# Execute the test
XDebugProbeFullJsonTest()
