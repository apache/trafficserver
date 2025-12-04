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
'''
Test to reproduce issue #12611 - retry fails when TSHttpTxnServerAddrSet is used
'''

import os
from ports import get_port

Test.Summary = '''
Reproduce issue #12611: OS_DNS hook is not called again on retry when using TSHttpTxnServerAddrSet
'''

plugin_name = "test_TSHttpTxnServerAddrSet_retry"


class TestIssue12611:
    """Reproduce issue #12611: retry fails when TSHttpTxnServerAddrSet is used."""

    def _configure_dns(self, tr: 'TestRun') -> 'Process':
        """Configure the DNS process for a TestRun."""
        self._dns = tr.MakeDNServer("dns", default=['127.0.0.1'])
        return self._dns

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        """Configure the Traffic Server process for a TestRun."""
        self._ts = tr.MakeATSProcess("ts", enable_cache=False)
        ts = self._ts

        plugin_path = os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', 'tsapi', '.libs', f'{plugin_name}.so')
        ts.Setup.Copy(plugin_path, ts.Env['PROXY_CONFIG_PLUGIN_PLUGIN_DIR'])
        ts.Disk.plugin_config.AddLine(f"{plugin_path}")

        ts.Disk.records_config.update(
            {
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|dns|test_TSHttpTxnServerAddrSet_retry',
                # Enable retries so we can see if OS_DNS is called multiple times
                'proxy.config.http.connect_attempts_max_retries': 3,
                'proxy.config.http.connect_attempts_timeout': 1,
            })

        # Remap to a nonexisting server - the plugin will set addresses
        bogus_port = get_port(ts, "bogus_port")
        ts.Disk.remap_config.AddLine(f'map / http://non.existent.server.com:{bogus_port}')

    def run(self):
        """Configure the TestRun."""
        tr = Test.AddTestRun("Reproduce issue #12611")
        self._configure_dns(tr)
        self._configure_traffic_server(tr)

        # Make a simple request - it should fail since first address is non-routable
        # but the plugin should log whether OS_DNS was called multiple times
        tr.Processes.Default.Command = f'curl -vs --connect-timeout 5 http://127.0.0.1:{self._ts.Variables.port}/ -o /dev/null 2>&1; true'
        tr.Processes.Default.ReturnCode = 0

        tr.Processes.Default.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._ts)

        tr.StillRunningAfter = self._ts

        # Check the diags.log for our diagnostic messages
        # After fix: OS_DNS should be called multiple times on retry
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression("OS_DNS hook called, count=1", "First OS_DNS call logged")

        # This message indicates the fix works - OS_DNS was called multiple times
        # Test will FAIL on master (bug exists), PASS after fix is applied
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            "SUCCESS: OS_DNS hook was called", "Plugin was able to retry with different address")


test = TestIssue12611()
test.run()
