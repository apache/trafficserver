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
from ports import get_port

Test.Summary = '''
Verify TSHttpTxnServerAddrSet() works as expected.
'''

plugin_name = "test_TSHttpTxnServerAddrSet"


class TestTSHttpTxnServerAddrSet:
    """Verify that TSHttpTxnServerAddrSet() works as expected."""
    _replay_file = "test_TSHttpTxnServerAddrSet.replay.yaml"

    def _configure_server(self, tr: 'TestRun') -> 'Process':
        """Configure the server process for a TestRun.

        :param tr: The TestRun for which to configure the server process.
        """
        self._server = tr.AddVerifierServerProcess("server", self._replay_file)
        self._server.Streams.stdout += Testers.ContainsExpression("redirect-succeeded", "Verify that the server sent the response.")
        return self._server

    def _configure_dns(self, tr: 'TestRun') -> 'Process':
        """Configure the DNS process for a TestRun.

        :param tr: The TestRun for which to configure the DNS process.
        """
        self._dns = tr.MakeDNServer("dns", default=['127.0.0.1'])
        return self._dns

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        """Configure the Traffic Server process for a TestRun.

        :param tr: The TestRun for which to configure the Traffic Server process.
        """
        self._ts = tr.MakeATSProcess("ts")
        ts = self._ts

        plugin_path = os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', 'tsapi', '.libs', f'{plugin_name}.so')
        ts.Setup.Copy(plugin_path, ts.Env['PROXY_CONFIG_PLUGIN_PLUGIN_DIR'])
        ts.Disk.plugin_config.AddLine(f"{plugin_path} 127.0.0.1 {self._server.Variables.http_port}")

        ts.Disk.records_config.update(
            {
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|dns|hostdb|test_plugin',
            })

        # Remap to a nonexisting server and port. The plugin should use
        # TSHttpTxnServerAddrSet() to set an actual server address and port. We
        # use get_port to guarantee that no one is listing on this port.
        bogus_port = get_port(self._server, "bogus_port")
        ts.Disk.remap_config.AddLine(f'map / http://non.existent.server.com:{bogus_port}')

    def run(self):
        """ Configure the TestRun."""
        tr = Test.AddTestRun("Verify TSHttpTxnServerAddrSet() works as expected.")
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        tr.AddVerifierClientProcess("client", self._replay_file, http_ports=[self._ts.Variables.port])

        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            "redirect-succeeded", "Verify that the client received the response.")

        tr.Processes.Default.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)


test = TestTSHttpTxnServerAddrSet()
test.run()
