"""
Verify abuse_shield plugin functionality.
"""
import sys

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
Verify abuse_shield plugin initialization and message handling via traffic_ctl.
'''

Test.SkipUnless(Condition.PluginExists('abuse_shield.so'),)


class AbuseShieldMessageTest:
    """Verify abuse_shield plugin message handling."""

    def __init__(self):
        """Set up the test environment and run all test scenarios."""
        self._setup_ts()
        self._test_plugin_initialization()
        self._test_disable_plugin()
        self._test_enable_plugin()
        self._test_dump_command()
        self._test_reload_command()

    def _setup_ts(self) -> None:
        """Configure ATS with the abuse_shield plugin."""
        self._ts = Test.MakeATSProcess("ts")

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'abuse_shield',
            })

        # Create the plugin config file.
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", id="abuse_shield_yaml", typename="ats:config")
        self._ts.Disk.abuse_shield_yaml.AddLines(
            '''
global:
  ip_tracking:
    slots: 1000

  blocking:
    duration_seconds: 60

rules:
  - name: "test_error_rule"
    filter:
      h2_error: 0x01
      min_count: 5
    action: [log, block]

  - name: "pure_attack_rule"
    filter:
      min_client_errors: 10
      max_successes: 0
    action: [log, block, close]

enabled: true
'''.strip().split('\n'))

        # Create trusted IPs file (YAML format).
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield_trusted.yaml", id="trusted_yaml", typename="ats:config")
        self._ts.Disk.trusted_yaml.AddLines('''
trusted_ips:
  - 127.0.0.1
'''.strip().split('\n'))

        # Configure abuse_shield plugin.
        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')

        # Verify the plugin loads. The plugin logs to diags.log via TSError.
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"abuse_shield.*Plugin initialized with 1000 slots per tracker, 2 rules",
            "Verify the abuse_shield plugin loaded successfully.")

    def _test_plugin_initialization(self) -> None:
        """Verify the plugin starts with configured values."""
        tr = Test.AddTestRun("Verify plugin starts with configured values.")
        tr.Processes.Default.Command = "echo verifying plugin starts with configured values"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        # The "Plugin initialized" NOTE message confirms the plugin loaded with correct config.
        # (The debug-level "Created 3 IP trackers" message only appears when debug output is enabled.)

    def _test_disable_plugin(self) -> None:
        """Verify the 'enabled' setting can be changed via traffic_ctl."""
        tr = Test.AddTestRun("Verify changing 'enabled' via traffic_ctl.")
        tr.Processes.Default.Command = "traffic_ctl plugin msg abuse_shield.enabled 0"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts

        tr = Test.AddTestRun("Await the enabled change.")
        tr.Processes.Default.Command = "echo awaiting enabled change"
        tr.Processes.Default.ReturnCode = 0
        await_enabled = tr.Processes.Process('await_enabled', 'sleep 30')
        await_enabled.Ready = When.FileContains(self._ts.Disk.diags_log.Name, "Plugin disabled")
        tr.Processes.Default.StartBefore(await_enabled)

        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            "Plugin disabled", "Verify abuse_shield received the disabled command.")

    def _test_enable_plugin(self) -> None:
        """Re-enable the plugin via traffic_ctl."""
        tr = Test.AddTestRun("Re-enable the plugin via traffic_ctl.")
        tr.Processes.Default.Command = "traffic_ctl plugin msg abuse_shield.enabled 1"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts

        tr = Test.AddTestRun("Await the re-enable.")
        tr.Processes.Default.Command = "echo awaiting re-enable"
        tr.Processes.Default.ReturnCode = 0
        await_reenable = tr.Processes.Process('await_reenable', 'sleep 30')
        await_reenable.Ready = When.FileContains(self._ts.Disk.diags_log.Name, "Plugin enabled")
        tr.Processes.Default.StartBefore(await_reenable)

        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            "Plugin enabled", "Verify abuse_shield received the enabled command.")

    def _test_dump_command(self) -> None:
        """Verify dump command via traffic_ctl."""
        tr = Test.AddTestRun("Verify dump command via traffic_ctl.")
        tr.Processes.Default.Command = "traffic_ctl plugin msg abuse_shield.dump"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts

        tr = Test.AddTestRun("Await the dump output.")
        tr.Processes.Default.Command = "echo awaiting dump output"
        tr.Processes.Default.ReturnCode = 0
        await_dump = tr.Processes.Process('await_dump', 'sleep 30')
        await_dump.Ready = When.FileContains(self._ts.Disk.diags_log.Name, "abuse_shield dump")
        tr.Processes.Default.StartBefore(await_dump)

        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            "abuse_shield.*Dump:", "Verify abuse_shield dump command works.")

    def _test_reload_command(self) -> None:
        """Verify reload command via traffic_ctl."""
        tr = Test.AddTestRun("Verify reload command via traffic_ctl.")
        tr.Processes.Default.Command = "traffic_ctl plugin msg abuse_shield.reload"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts

        tr = Test.AddTestRun("Await the reload.")
        tr.Processes.Default.Command = "echo awaiting reload"
        tr.Processes.Default.ReturnCode = 0
        await_reload = tr.Processes.Process('await_reload', 'sleep 30')
        await_reload.Ready = When.FileContains(self._ts.Disk.diags_log.Name, "Configuration reloaded successfully")
        tr.Processes.Default.StartBefore(await_reload)

        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            "Configuration reloaded successfully", "Verify abuse_shield reload command works.")


class AbuseShieldRateLimitTest:
    """Verify abuse_shield plugin can detect and block request rate floods.

    This test sends HTTP/2 requests at a rate exceeding the configured
    max_req_rate threshold and verifies that the plugin detects this and
    blocks the offending IP.
    """

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self):
        """Set up the test environment and run rate limit test scenarios."""
        self._setup_origin_server()
        self._setup_ts()
        self._test_rate_limit_exceeded()

    def _setup_origin_server(self) -> None:
        """Configure a simple HTTP/1.1 origin server."""
        name = f'origin{AbuseShieldRateLimitTest._server_counter}'
        AbuseShieldRateLimitTest._server_counter += 1

        self._origin = Test.MakeOriginServer(name)

        # Add a simple response for GET requests.
        self._origin.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    "HTTP/1.1 200 OK\r\nServer: origin\r\nCache-Control: max-age=300\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "OK"
            })

    def _setup_ts(self) -> None:
        """Configure ATS with TLS and the abuse_shield plugin for rate limiting."""
        name = f'ts_rate{AbuseShieldRateLimitTest._ts_counter}'
        AbuseShieldRateLimitTest._ts_counter += 1

        self._ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()

        # Configure SSL for ATS.
        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

        # Remap to the origin server.
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin.Variables.Port}/')

        # Configure records.
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'abuse_shield',
                'proxy.config.http.insert_response_via_str': 2,
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            })

        # Create the plugin config file with a low max_req_rate for testing.
        # With max_req_rate: 20, sending 50 requests should trigger the rule.
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", id="abuse_shield_yaml", typename="ats:config")
        self._ts.Disk.abuse_shield_yaml.AddLines(
            '''
global:
  ip_tracking:
    slots: 1000

  blocking:
    duration_seconds: 60

rules:
  - name: "req_rate_flood"
    filter:
      max_req_rate: 20
    action: [log, block]

enabled: true
'''.strip().split('\n'))

        # Configure abuse_shield plugin.
        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')

        # Verify the plugin loads.
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"abuse_shield.*Plugin initialized with 1000 slots per tracker, 1 rules",
            "Verify the abuse_shield plugin loaded with rate limit rule.")

    def _test_rate_limit_exceeded(self) -> None:
        """Send requests exceeding the rate threshold and verify blocking.

        This test sends 50 requests at 100 req/sec, which should exceed the
        max_req_rate of 20 and trigger the req_rate_flood rule.
        """
        tr = Test.AddTestRun("Send excessive H2 requests to trigger rate limit")

        # Send 50 requests at high rate - this should exceed max_req_rate of 20.
        client_cmd = (
            f'{sys.executable} {Test.TestDirectory}/h2_rate_client.py '
            f'--host localhost --port {self._ts.Variables.ssl_port} '
            f'--num-requests 50 --rate 100 --path /')
        tr.Processes.Default.Command = client_cmd
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        # Verify the rate limit rule was triggered and block action was taken.
        # The plugin logs via TSError to diags.log when a rule matches.
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rule "req_rate_flood" matched for IP=.*actions=\[log,block\]',
            "Verify the req_rate_flood rule was triggered and block action was taken.")


class AbuseShieldConnRateTest:
    """Verify abuse_shield plugin can detect and block connection rate floods.

    This test opens many connections rapidly to exceed the configured
    max_conn_rate threshold and verifies that the plugin detects this and
    blocks the offending IP.
    """

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self):
        """Set up the test environment and run connection rate test scenarios."""
        self._setup_origin_server()
        self._setup_ts()
        self._test_conn_rate_exceeded()

    def _setup_origin_server(self) -> None:
        """Configure a simple HTTP/1.1 origin server."""
        name = f'origin_conn{AbuseShieldConnRateTest._server_counter}'
        AbuseShieldConnRateTest._server_counter += 1

        self._origin = Test.MakeOriginServer(name)

        self._origin.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    "HTTP/1.1 200 OK\r\nServer: origin\r\nCache-Control: max-age=300\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "OK"
            })

    def _setup_ts(self) -> None:
        """Configure ATS with TLS and the abuse_shield plugin for connection rate limiting."""
        name = f'ts_conn{AbuseShieldConnRateTest._ts_counter}'
        AbuseShieldConnRateTest._ts_counter += 1

        self._ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()

        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin.Variables.Port}/')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'abuse_shield',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            })

        # Create the plugin config file with a low max_conn_rate for testing.
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", id="abuse_shield_yaml", typename="ats:config")
        self._ts.Disk.abuse_shield_yaml.AddLines(
            '''
global:
  ip_tracking:
    slots: 1000

  blocking:
    duration_seconds: 60

rules:
  - name: "conn_rate_flood"
    filter:
      max_conn_rate: 5
    action: [log, block]

enabled: true
'''.strip().split('\n'))

        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')

        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"abuse_shield.*Plugin initialized with 1000 slots per tracker, 1 rules",
            "Verify the abuse_shield plugin loaded with connection rate limit rule.")

    def _test_conn_rate_exceeded(self) -> None:
        """Open many connections rapidly to exceed the connection rate threshold."""
        tr = Test.AddTestRun("Send excessive connections to trigger connection rate limit")

        # Open many separate connections rapidly - this should exceed max_conn_rate of 5.
        # Use seq piped to xargs. Some curls may fail when blocked (expected), so use || true.
        client_cmd = (f'seq 1 30 | xargs -P 30 -I {{}} '
                      f'curl -k -s https://127.0.0.1:{self._ts.Variables.ssl_port}/ || true')
        tr.Processes.Default.Command = client_cmd
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        # Verify the connection rate rule was triggered and block action was taken.
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rule "conn_rate_flood" matched for IP=.*actions=\[log,block\]',
            "Verify the conn_rate_flood rule was triggered and block action was taken.")


class AbuseShieldHTTPBlockTest:
    """Verify abuse_shield plugin blocks plain HTTP connections via SSN_START hook.

    This test sends HTTP/1.1 requests (not HTTPS) to verify that blocking
    works for plain HTTP connections using the SSN_START hook.
    """

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self):
        """Set up the test environment and run HTTP blocking test scenarios."""
        self._setup_origin_server()
        self._setup_ts()
        self._test_http_rate_limit()

    def _setup_origin_server(self) -> None:
        """Configure a simple HTTP/1.1 origin server."""
        name = f'origin_http{AbuseShieldHTTPBlockTest._server_counter}'
        AbuseShieldHTTPBlockTest._server_counter += 1

        self._origin = Test.MakeOriginServer(name)

        self._origin.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    "HTTP/1.1 200 OK\r\nServer: origin\r\nCache-Control: max-age=300\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "OK"
            })

    def _setup_ts(self) -> None:
        """Configure ATS for plain HTTP (no TLS) with the abuse_shield plugin."""
        name = f'ts_http{AbuseShieldHTTPBlockTest._ts_counter}'
        AbuseShieldHTTPBlockTest._ts_counter += 1

        self._ts = Test.MakeATSProcess(name, enable_tls=False, enable_cache=True)

        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin.Variables.Port}/')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'abuse_shield',
            })

        # Create the plugin config file with a low max_req_rate for testing.
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", id="abuse_shield_yaml", typename="ats:config")
        self._ts.Disk.abuse_shield_yaml.AddLines(
            '''
global:
  ip_tracking:
    slots: 1000

  blocking:
    duration_seconds: 60

rules:
  - name: "http_req_rate_flood"
    filter:
      max_req_rate: 10
    action: [log, block]

enabled: true
'''.strip().split('\n'))

        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')

        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"abuse_shield.*Plugin initialized with 1000 slots per tracker, 1 rules",
            "Verify the abuse_shield plugin loaded with HTTP rate limit rule.")

    def _test_http_rate_limit(self) -> None:
        """Send HTTP/1.1 requests exceeding the rate threshold and verify blocking."""
        tr = Test.AddTestRun("Send excessive HTTP/1.1 requests to trigger rate limit")

        # Send many HTTP/1.1 requests - this should exceed max_req_rate of 10.
        # Use seq piped to xargs. Some curls may fail when blocked (expected), so use || true.
        client_cmd = (f'seq 1 50 | xargs -P 50 -I {{}} '
                      f'curl -s http://127.0.0.1:{self._ts.Variables.port}/ || true')
        tr.Processes.Default.Command = client_cmd
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        # Verify the rate limit rule was triggered for HTTP and block action was taken.
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rule "http_req_rate_flood" matched for IP=.*actions=\[log,block\]',
            "Verify the http_req_rate_flood rule was triggered and block action was taken.")


class AbuseShieldMultipleRulesTest:
    """Verify abuse_shield plugin can handle multiple rules with different thresholds.

    This test verifies that when multiple rules are configured, the first matching
    rule triggers blocking. Combined rules (AND logic) with connection rate require
    multiple physical connections and are better tested manually.
    """

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self):
        """Set up the test environment and run multiple rules test scenarios."""
        self._setup_origin_server()
        self._setup_ts()
        self._test_multiple_rules()

    def _setup_origin_server(self) -> None:
        """Configure a simple HTTP/1.1 origin server."""
        name = f'origin_multi{AbuseShieldMultipleRulesTest._server_counter}'
        AbuseShieldMultipleRulesTest._server_counter += 1

        self._origin = Test.MakeOriginServer(name)

        self._origin.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    "HTTP/1.1 200 OK\r\nServer: origin\r\nCache-Control: max-age=300\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "OK"
            })

    def _setup_ts(self) -> None:
        """Configure ATS with multiple rules with different thresholds."""
        name = f'ts_multi{AbuseShieldMultipleRulesTest._ts_counter}'
        AbuseShieldMultipleRulesTest._ts_counter += 1

        self._ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()

        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin.Variables.Port}/')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'abuse_shield',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            })

        # Create plugin config with multiple rules - first match wins.
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", id="abuse_shield_yaml", typename="ats:config")
        self._ts.Disk.abuse_shield_yaml.AddLines(
            '''
global:
  ip_tracking:
    slots: 1000

  blocking:
    duration_seconds: 60

rules:
  - name: "strict_limit"
    filter:
      max_req_rate: 15
    action: [log, block]

  - name: "lenient_limit"
    filter:
      max_req_rate: 100
    action: [log]

enabled: true
'''.strip().split('\n'))

        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')

        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"abuse_shield.*Plugin initialized with 1000 slots per tracker, 2 rules",
            "Verify the abuse_shield plugin loaded with multiple rules.")

    def _test_multiple_rules(self) -> None:
        """Exceed rate to trigger the first matching rule."""
        tr = Test.AddTestRun("Trigger first matching rule (strict_limit)")

        # Send requests via H2 rate client to exceed the strict_limit threshold.
        client_cmd = (
            f'{sys.executable} {Test.TestDirectory}/h2_rate_client.py '
            f'--host localhost --port {self._ts.Variables.ssl_port} '
            f'--num-requests 50 --rate 100 --path /')
        tr.Processes.Default.Command = client_cmd
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        # Verify the strict_limit rule was triggered (first match wins) and block action was taken.
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rule "strict_limit" matched for IP=.*actions=\[log,block\]',
            "Verify the strict_limit rule was triggered and block action was taken.")


class AbuseShieldBlockExpirationTest:
    """Verify abuse_shield plugin block expiration works correctly.

    After a block expires (duration_seconds), the IP should be able to
    make requests again.
    """

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self):
        """Set up the test environment and run block expiration test scenarios."""
        self._setup_origin_server()
        self._setup_ts()
        self._test_block_expiration()

    def _setup_origin_server(self) -> None:
        """Configure a simple HTTP/1.1 origin server."""
        name = f'origin_expire{AbuseShieldBlockExpirationTest._server_counter}'
        AbuseShieldBlockExpirationTest._server_counter += 1

        self._origin = Test.MakeOriginServer(name)

        self._origin.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    "HTTP/1.1 200 OK\r\nServer: origin\r\nCache-Control: max-age=300\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "OK"
            })

    def _setup_ts(self) -> None:
        """Configure ATS with a short block duration for testing expiration."""
        name = f'ts_expire{AbuseShieldBlockExpirationTest._ts_counter}'
        AbuseShieldBlockExpirationTest._ts_counter += 1

        self._ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()

        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin.Variables.Port}/')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'abuse_shield',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            })

        # Create plugin config with a very short block duration (5 seconds) for testing.
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", id="abuse_shield_yaml", typename="ats:config")
        self._ts.Disk.abuse_shield_yaml.AddLines(
            '''
global:
  ip_tracking:
    slots: 1000

  blocking:
    duration_seconds: 5

rules:
  - name: "short_block"
    filter:
      max_req_rate: 10
    action: [log, block]

enabled: true
'''.strip().split('\n'))

        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')

        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"abuse_shield.*Plugin initialized with 1000 slots per tracker, 1 rules",
            "Verify the abuse_shield plugin loaded with short block duration.")

    def _test_block_expiration(self) -> None:
        """Trigger a block, wait for expiration, and verify requests work again."""
        # Step 1: Trigger a block by exceeding rate limit.
        tr = Test.AddTestRun("Trigger block by exceeding rate limit")

        client_cmd = (
            f'{sys.executable} {Test.TestDirectory}/h2_rate_client.py '
            f'--host localhost --port {self._ts.Variables.ssl_port} '
            f'--num-requests 50 --rate 100 --path /')
        tr.Processes.Default.Command = client_cmd
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        # Verify blocking occurred.
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rule "short_block" matched for IP=.*actions=\[log,block\]',
            "Verify the short_block rule was triggered and block action was taken.")

        # Step 2: Wait for block to expire (5 seconds + buffer).
        tr = Test.AddTestRun("Wait for block to expire")
        tr.Processes.Default.Command = "sleep 7"
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts

        # Step 3: Verify requests work again after expiration.
        tr = Test.AddTestRun("Verify requests work after block expires")
        tr.Processes.Default.Command = f'curl -k -s -o /dev/null -w "%{{http_code}}" https://127.0.0.1:{self._ts.Variables.ssl_port}/'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200", "Verify request succeeds after block expires.")
        tr.StillRunningAfter = self._ts


class AbuseShieldCombinedRuleTest:
    """Verify abuse_shield combined rule (conn_rate AND req_rate).

    Using curl (which disconnects after each request), each request
    creates a new connection. This allows testing combined rules that
    require BOTH connection rate AND request rate to be exceeded.
    """

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self):
        """Set up the test environment and run combined rule test scenarios."""
        self._setup_origin_server()
        self._setup_ts()
        self._test_combined_rule()

    def _setup_origin_server(self) -> None:
        """Configure a simple HTTP/1.1 origin server."""
        name = f'origin_combined{AbuseShieldCombinedRuleTest._server_counter}'
        AbuseShieldCombinedRuleTest._server_counter += 1

        self._origin = Test.MakeOriginServer(name)

        self._origin.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    "HTTP/1.1 200 OK\r\nServer: origin\r\nCache-Control: max-age=300\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "OK"
            })

    def _setup_ts(self) -> None:
        """Configure ATS with a combined rule requiring both conn_rate AND req_rate."""
        name = f'ts_combined{AbuseShieldCombinedRuleTest._ts_counter}'
        AbuseShieldCombinedRuleTest._ts_counter += 1

        self._ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()

        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin.Variables.Port}/')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'abuse_shield',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            })

        # Create plugin config with a combined rule (AND logic).
        # Both max_conn_rate AND max_req_rate must be exceeded to trigger.
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", id="abuse_shield_yaml", typename="ats:config")
        self._ts.Disk.abuse_shield_yaml.AddLines(
            '''
global:
  ip_tracking:
    slots: 1000

  blocking:
    duration_seconds: 60

rules:
  - name: "combined_abuse"
    filter:
      max_conn_rate: 5
      max_req_rate: 10
    action: [log, block]

enabled: true
'''.strip().split('\n'))

        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')

        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"abuse_shield.*Plugin initialized with 1000 slots per tracker, 1 rules",
            "Verify the abuse_shield plugin loaded with combined rule.")

    def _test_combined_rule(self) -> None:
        """Send requests to exceed both conn_rate and req_rate thresholds."""
        tr = Test.AddTestRun("Send requests to trigger combined rule (conn_rate AND req_rate)")

        # Each curl creates a new connection AND a new request.
        # Sending 30 parallel curls will exceed both:
        # - max_conn_rate: 5 (30 connections > 5)
        # - max_req_rate: 10 (30 requests > 10)
        # Some curls may fail when blocked (expected), so use || true.
        client_cmd = (f'seq 1 30 | xargs -P 30 -I {{}} '
                      f'curl -k -s https://127.0.0.1:{self._ts.Variables.ssl_port}/ || true')
        tr.Processes.Default.Command = client_cmd
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        # Verify the combined rule was triggered and block action was taken.
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rule "combined_abuse" matched for IP=.*actions=\[log,block\]',
            "Verify the combined_abuse rule was triggered and block action was taken.")


class AbuseShieldLogFileTest:
    """Verify abuse_shield plugin writes to a separate log file when configured.

    This test configures the plugin with a log_file setting and verifies that
    LOG action output goes to the separate log file instead of diags.log.
    """

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self):
        """Set up the test environment and run log file test scenarios."""
        self._setup_origin_server()
        self._setup_ts()
        self._test_log_file_output()

    def _setup_origin_server(self) -> None:
        """Configure a simple HTTP/1.1 origin server."""
        name = f'origin_logfile{AbuseShieldLogFileTest._server_counter}'
        AbuseShieldLogFileTest._server_counter += 1

        self._origin = Test.MakeOriginServer(name)

        self._origin.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers":
                    "HTTP/1.1 200 OK\r\nServer: origin\r\nCache-Control: max-age=300\r\nConnection: close\r\nContent-Length: 2\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "OK"
            })

    def _setup_ts(self) -> None:
        """Configure ATS with the abuse_shield plugin and a separate log file."""
        name = f'ts_logfile{AbuseShieldLogFileTest._ts_counter}'
        AbuseShieldLogFileTest._ts_counter += 1

        self._ts = Test.MakeATSProcess(name, enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()

        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._origin.Variables.Port}/')

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'abuse_shield',
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            })

        # Create the plugin config file with a log_file setting.
        self._ts.Disk.File(self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", id="abuse_shield_yaml", typename="ats:config")
        self._ts.Disk.abuse_shield_yaml.AddLines(
            '''
global:
  ip_tracking:
    slots: 1000

  blocking:
    duration_seconds: 60

  log_file: abuse_shield_actions

rules:
  - name: "log_test_rule"
    filter:
      max_req_rate: 10
    action: [log, block]

enabled: true
'''.strip().split('\n'))

        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')

        # Verify the plugin loads.
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"abuse_shield.*Plugin initialized with 1000 slots per tracker, 1 rules",
            "Verify the abuse_shield plugin loaded with log file configuration.")

    def _test_log_file_output(self) -> None:
        """Trigger rate limit and verify output appears in separate log file."""
        tr = Test.AddTestRun("Trigger rate limit to test log file output")

        # Send requests via H2 rate client to exceed the rate limit threshold.
        client_cmd = (
            f'{sys.executable} {Test.TestDirectory}/h2_rate_client.py '
            f'--host localhost --port {self._ts.Variables.ssl_port} '
            f'--num-requests 50 --rate 100 --path /')
        tr.Processes.Default.Command = client_cmd
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

        # Verify the log file was created and contains the expected output.
        # The log file is created in the log directory with .log extension.
        log_file_path = self._ts.Variables.LOGDIR + "/abuse_shield_actions.log"
        self._ts.Disk.File(log_file_path, id="abuse_shield_log", typename="ats:config")
        self._ts.Disk.abuse_shield_log.Content = Testers.ContainsExpression(
            r'Rule "log_test_rule" matched for IP=', "Verify log file contains rule match entry.")
        self._ts.Disk.abuse_shield_log.Content += Testers.ContainsExpression(
            r'req_tokens=', "Verify log file contains token count information.")


#
# Main: Run the tests.
#
AbuseShieldMessageTest()
AbuseShieldRateLimitTest()
AbuseShieldConnRateTest()
AbuseShieldHTTPBlockTest()
AbuseShieldMultipleRulesTest()
AbuseShieldBlockExpirationTest()
AbuseShieldCombinedRuleTest()
AbuseShieldLogFileTest()
