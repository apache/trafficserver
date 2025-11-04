'''
Verify cookie_remap plugin's disable_pristine_host_hdr functionality.
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

import os

Test.Summary = '''
Test cookie_remap plugin's disable_pristine_host_hdr functionality. Verifies that
when disable_pristine_host_hdr is set to true, the pristine host header setting
is disabled for the matched transaction, allowing the Host header to be updated
to match the remapped destination.
'''
Test.SkipUnless(Condition.PluginExists('cookie_remap.so'))
Test.ContinueOnFail = True


class TestUpdateHostHeader:
    """
    Test the disable_pristine_host_hdr feature of cookie_remap plugin.

    This test verifies that:
    1. Cookie bucket routing works correctly
    2. When disable_pristine_host_hdr is enabled, the Host header is updated
       to match the sendto URL destination
    3. When disable_pristine_host_hdr is not set, the Host header remains as
       the original client value
    """

    # Counter for unique process names across multiple test instances
    test_counter: int = 0

    def __init__(self, disable_pristine_host_hdr=True):
        """Initialize the test by setting up servers and ATS configuration.
        :param disable_pristine_host_hdr: Whether to configure disable_pristine_host_hdr
          in the cookie_remap configuration.
        """
        self.test_id = TestUpdateHostHeader.test_counter
        TestUpdateHostHeader.test_counter += 1
        self.disable_pristine_host_hdr = disable_pristine_host_hdr
        self.replay_file = f'disable_pristine_host_hdr_{"true" if disable_pristine_host_hdr else "false"}.replay.yaml'
        self._setupDns()
        self._setupServers()
        self._setupTS(disable_pristine_host_hdr)
        self._setupClient()

    def _setupDns(self):
        """Configure the DNS server."""
        self._dns = Test.MakeDNServer(f"dns_{self.test_id}", default='127.0.0.1')

    def _setupServers(self):
        """
        Configure the origin servers using proxy-verifier.

        Creates two servers to simulate canary and stable environments.
        """
        self._server_canary = Test.MakeVerifierServerProcess(f"server_canary_{self.test_id}", self.replay_file)
        expected_host = 'canary.com' if self.disable_pristine_host_hdr else 'example.com'
        self._server_canary.Streams.All += Testers.ContainsExpression(expected_host, f'Host header should be {expected_host}')

        self._server_stable = Test.MakeVerifierServerProcess(f"server_stable_{self.test_id}", self.replay_file)
        # The else path always preserves pristine host header (example.com).
        expected_host = 'example.com'
        self._server_stable.Streams.All += Testers.ContainsExpression(expected_host, f'Host header should be {expected_host}')

    def _setupTS(self, disable_pristine_host_hdr):
        """Configure Traffic Server with cookie_remap plugin.
        :param disable_pristine_host_hdr: Whether to configure disable_pristine_host_hdr in cookie_remap.
        """
        ts = Test.MakeATSProcess(f"ts_{self.test_id}", enable_cache=False)
        self._ts = ts

        # Enable debug logging for cookie_remap and enable pristine_host_hdr
        # (simulating production environment)
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'cookie_remap|http',
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.url_remap.pristine_host_hdr': 1,
            })

        # Read and configure the cookie_remap configuration file
        config_filename = f'disable_pristine_host_hdr_config_{"true" if disable_pristine_host_hdr else "false"}.txt'
        config_path = os.path.join(Test.TestDirectory, f"configs/{config_filename}")
        with open(config_path, 'r') as config_file:
            config_content = config_file.read()
        config_content = config_content.replace("$CANARY_PORT", str(self._server_canary.Variables.http_port))
        config_content = config_content.replace("$STABLE_PORT", str(self._server_stable.Variables.http_port))
        ts.Disk.File(ts.Variables.CONFIGDIR + f"/{config_filename}", id="cookie_config")
        ts.Disk.cookie_config.WriteOn(config_content)

        # Configure remap rule with cookie_remap plugin
        ts.Disk.remap_config.AddLine(
            'map http://example.com http://shouldnothit.com '
            f'@plugin=cookie_remap.so @pparam=config/{config_filename}')

    def _setupClient(self):
        """Setup the client for the test."""
        enabled_str = "enabled" if self.disable_pristine_host_hdr else "disabled"
        tr = Test.AddTestRun(f'Test cookie bucket routing with disable_pristine_host_hdr {enabled_str}')

        p = tr.AddVerifierClientProcess(f'client_{self.test_id}', self.replay_file, http_ports=[self._ts.Variables.port])
        p.StartBefore(self._dns)
        p.StartBefore(self._ts)
        p.StartBefore(self._server_canary)
        p.StartBefore(self._server_stable)


# Execute the test
TestUpdateHostHeader(disable_pristine_host_hdr=True)
TestUpdateHostHeader(disable_pristine_host_hdr=False)
