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
Tests for TSHttpTxnServerAddrSet() retry behavior with and without cache enabled.
'''

import os
from ports import get_port

Test.Summary = '''
Verify TSHttpTxnServerAddrSet() retry works with and without cache enabled
'''

plugin_name = "test_TSHttpTxnServerAddrSet_retry"


class TestServerAddrSetRetry:
    """Verify retry behavior for TSHttpTxnServerAddrSet() with and without cache."""

    def _configure_dns(self, tr: 'TestRun', name: str) -> 'Process':
        """Configure the DNS process for a TestRun."""
        return tr.MakeDNServer(name, default=['127.0.0.1'])

    def _configure_traffic_server(self, tr: 'TestRun', name: str, dns: 'Process', enable_cache: bool) -> 'Process':
        """Configure the Traffic Server process for a TestRun."""
        ts = tr.MakeATSProcess(name, enable_cache=enable_cache)

        plugin_path = os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'pluginTest', 'tsapi', '.libs', f'{plugin_name}.so')
        ts.Setup.Copy(plugin_path, ts.Env['PROXY_CONFIG_PLUGIN_PLUGIN_DIR'])
        ts.Disk.plugin_config.AddLine(f"{plugin_path}")

        ts.Disk.records_config.update(
            {
                'proxy.config.dns.nameservers': f'127.0.0.1:{dns.Variables.Port}',
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
        return ts

    def run(self, enable_cache: bool, description: str, name_suffix: str) -> None:
        """Configure a TestRun."""
        tr = Test.AddTestRun(description)
        dns = self._configure_dns(tr, f"dns_{name_suffix}")
        ts = self._configure_traffic_server(tr, f"ts_{name_suffix}", dns, enable_cache)

        # Make a simple request - it should fail since first address is non-routable
        # but the plugin should log whether OS_DNS was called multiple times
        tr.Processes.Default.Command = f'curl -vs --connect-timeout 5 http://127.0.0.1:{ts.Variables.port}/ -o /dev/null 2>&1; true'
        tr.Processes.Default.ReturnCode = 0

        tr.Processes.Default.StartBefore(dns)
        tr.Processes.Default.StartBefore(ts)

        # Override the default diags.log error check - we use TSError() for test output
        # Using '=' instead of '+=' replaces the default "no errors" check
        ts.Disk.diags_log.Content = Testers.ContainsExpression("OS_DNS hook called, count=1", "First OS_DNS call logged")

        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            "SUCCESS: OS_DNS hook was called", "Plugin was able to retry with different address")

        if enable_cache:
            ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
                "failed assertion", "ATS should not hit the redirect write-lock assertion")
            ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
                "received signal 6", "ATS should not abort while retrying origin selection")


test = TestServerAddrSetRetry()
test.run(enable_cache=False, description="Reproduce issue #12611 without cache", name_suffix="nocache")
test.run(enable_cache=True, description="Verify TSHttpTxnServerAddrSet retry is safe with cache enabled", name_suffix="cache")
