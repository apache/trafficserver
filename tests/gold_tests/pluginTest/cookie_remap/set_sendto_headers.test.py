'''
Verify cookie_remap plugin's set_sendto_headers functionality.
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
Test cookie_remap plugin's set_sendto_headers functionality. Verifies that:
1. Headers can be set dynamically based on cookie rules
2. Host header setting automatically disables pristine host header
3. Regex capture groups work in header values ($1, $2, etc.)
4. Path variables work in header values ($path, etc.)
5. Multiple headers can be set simultaneously
6. Headers are ONLY set on sendto path, NOT on non-matching (else) path
'''
Test.SkipUnless(Condition.PluginExists('cookie_remap.so'))
Test.ContinueOnFail = True


class TestSetSendtoHeaders:
    """
    Test the set_sendto_headers feature of cookie_remap plugin.

    This test verifies that:
    1. Simple header setting works (including Host)
    2. Regex capture groups are substituted correctly in header values
    3. Path variables are substituted correctly in header values
    4. Multiple headers can be set
    5. When Host header is set, pristine_host_hdr is automatically disabled
    6. When Host header is NOT set, pristine_host_hdr behavior is preserved
    7. Headers are ONLY set on sendto path (not on non-matching requests)
    """

    def __init__(self):
        """Initialize the test by setting up servers and ATS configuration."""
        self.replay_file = 'set_sendto_headers.replay.yaml'
        self._setupDns()
        self._setupServers()
        self._setupTS()
        self._setupClient()

    def _setupDns(self):
        """Configure the DNS server."""
        self._dns = Test.MakeDNServer("dns", default='127.0.0.1')

    def _setupServers(self):
        """
        Configure the origin servers using proxy-verifier.

        Creates multiple servers to simulate different backend services.
        """
        # Server for simple host header test
        self._server_backend = Test.MakeVerifierServerProcess("server_backend", self.replay_file)
        self._server_backend.Streams.All += Testers.ContainsExpression(
            'backend.com', 'Host header should be backend.com for test 1')
        self._server_backend.Streams.All += Testers.ContainsExpression('custom-value', 'X-Custom-Header should be set for test 1')

        # Server for regex capture group tests (premium and standard)
        self._server_service = Test.MakeVerifierServerProcess("server_service", self.replay_file)
        self._server_service.Streams.All += Testers.ContainsExpression(
            'premium.service.com', 'Host header should be premium.service.com for premium tier')
        self._server_service.Streams.All += Testers.ContainsExpression(
            'standard.service.com', 'Host header should be standard.service.com for standard tier')

        # Server for path variable test
        self._server_debug = Test.MakeVerifierServerProcess("server_debug", self.replay_file)

        # Server for no-host test (pristine host preserved)
        self._server_nohost = Test.MakeVerifierServerProcess("server_nohost", self.replay_file)
        self._server_nohost.Streams.All += Testers.ContainsExpression(
            'example.com', 'Host header should be example.com (pristine) when Host not set in set_sendto_headers')

        # Server for non-matching cookie test (verifies headers are NOT set on non-sendto path)
        self._server_nomatch = Test.MakeVerifierServerProcess("server_nomatch", self.replay_file)
        self._server_nomatch.Streams.All += Testers.ExcludesExpression(
            'X-Custom-Header', 'Custom headers should NOT be present on non-sendto path')
        self._server_nomatch.Streams.All += Testers.ExcludesExpression(
            'X-User-Tier', 'Custom headers should NOT be present on non-sendto path')

    def _setupTS(self):
        """Configure Traffic Server with cookie_remap plugin."""
        ts = Test.MakeATSProcess("ts", enable_cache=False)
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
        config_filename = 'set_sendto_headers_config.yaml'
        config_path = os.path.join(Test.TestDirectory, f"configs/{config_filename}")
        with open(config_path, 'r') as config_file:
            config_content = config_file.read()

        # Replace port placeholders
        config_content = config_content.replace("$BACKEND_PORT", str(self._server_backend.Variables.http_port))
        config_content = config_content.replace("$SERVICE_PORT", str(self._server_service.Variables.http_port))
        config_content = config_content.replace("$DEBUG_PORT", str(self._server_debug.Variables.http_port))
        config_content = config_content.replace("$NOHOST_PORT", str(self._server_nohost.Variables.http_port))
        config_content = config_content.replace("$NOMATCH_PORT", str(self._server_nomatch.Variables.http_port))

        ts.Disk.File(ts.Variables.CONFIGDIR + f"/{config_filename}", id="cookie_config")
        ts.Disk.cookie_config.WriteOn(config_content)

        # Configure remap rule with cookie_remap plugin
        # The default target should point to the nomatch server for testing
        ts.Disk.remap_config.AddLine(
            f'map http://example.com http://127.0.0.1:{self._server_nomatch.Variables.http_port} '
            f'@plugin=cookie_remap.so @pparam=config/{config_filename}')

    def _setupClient(self):
        """Setup the client for the test."""
        tr = Test.AddTestRun('Test cookie_remap set_sendto_headers functionality')

        p = tr.AddVerifierClientProcess('client', self.replay_file, http_ports=[self._ts.Variables.port])
        p.StartBefore(self._dns)
        p.StartBefore(self._ts)
        p.StartBefore(self._server_backend)
        p.StartBefore(self._server_service)
        p.StartBefore(self._server_debug)
        p.StartBefore(self._server_nohost)
        p.StartBefore(self._server_nomatch)


# Execute the test
TestSetSendtoHeaders()
