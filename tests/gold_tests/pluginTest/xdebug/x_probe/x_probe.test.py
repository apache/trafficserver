'''
Verify xdebug plugin probe functionality.
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

Test.Summary = 'Test xdebug plugin probe functionality'
Test.ContinueOnFail = True
Test.SkipUnless(Condition.PluginExists('xdebug.so'))


class XDebugProbeTest:
    """
    Test the xdebug probe functionality which transforms the response body
    to include request and response headers in a multipart format.

    The probe feature:
    - Changes Content-Type to text/plain
    - Injects request headers before the original body
    - Injects response headers after the original body
    - Uses multipart boundary separators
    - Disables caching due to body modification
    """

    _replay_file: str = "x_probe.replay.yaml"

    def __init__(self) -> None:
        tr = Test.AddTestRun("xdebug probe test")
        self._setupOriginServer(tr)
        self._setupTS(tr)
        self._setupClient(tr)

    def _setupOriginServer(self, tr: 'TestRun') -> None:
        """Configure the origin server using Proxy Verifier.
        :param tr: TestRun to add the server to.
        """
        self._server = tr.AddVerifierServerProcess("server", self._replay_file)

    def _setupTS(self, tr: 'TestRun') -> None:
        """Configure ATS with xdebug plugin enabled for probe functionality.
        :param tr: TestRun to add the ATS process to.
        """
        self._ts = Test.MakeATSProcess("ts")

        self._ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "xdebug",
        })

        self._ts.Disk.plugin_config.AddLine('xdebug.so --enable=probe')
        self._ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.http_port}")

    def _setupClient(self, tr: 'TestRun') -> None:
        """Test basic probe functionality with header injection.
        :param tr: TestRun to add the test to.
        """
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.AddVerifierClientProcess("client", self._replay_file, http_ports=[self._ts.Variables.port])
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'ATS xDebug Probe Injection Boundary', "ATS xDebug Probe Injection Boundary should be present")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('xDebugProbeAt', "xDebugProbeAt should be present")


# Execute the test
XDebugProbeTest()
