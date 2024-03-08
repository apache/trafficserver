'''
Test the exit_on_load_fail behavior for the SSL cert loading.
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
Test the exit_on_load_fail behavior for the SSL cert loading.
'''


class Test_exit_on_cert_load_fail:
    """Configure a test to verify behavior of SSL cert load failures."""

    ts_counter: int = 0

    def __init__(self, name: str, is_testing_server_side: bool, enable_exit_on_load=False):
        """
        Initialize the test.
        :param name: The name of the test.
        :param is_testing_server_side: Whether to test the server side or client side cert loading.
        enable_exit_on_load: Whether to enable the exit_on_load_fail config.
        """
        self.name = name
        self.is_testing_server_side = is_testing_server_side
        self.enable_exit_on_load = enable_exit_on_load

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = tr.MakeATSProcess(f"ts-{Test_exit_on_cert_load_fail.ts_counter}", enable_tls=True)
        Test_exit_on_cert_load_fail.ts_counter += 1
        self._ts = ts
        client_cert_path = 'NULL'
        enable_exit_on_server_cert_load_failure = 1 if self.enable_exit_on_load and self.is_testing_server_side else 0
        enable_exit_on_client_cert_load_failure = 1 if self.enable_exit_on_load and not self.is_testing_server_side else 0

        if not self.is_testing_server_side:
            # Point to a non-existent cert to force a load failure.
            client_cert_path = f'{ts.Variables.SSLDir}/non-existent-cert.pem'
            # Also setup the server certs so that issues are limited to client
            # cert loading.
            self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self._ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.client.cert.filename': client_cert_path,
                'proxy.config.ssl.server.multicert.exit_on_load_fail': enable_exit_on_server_cert_load_failure,
                'proxy.config.ssl.client.cert.exit_on_load_fail': enable_exit_on_client_cert_load_failure,
            })
        # The cert loading happen on startup, so the remap rule is not
        # triggered.
        RANDOM_PORT = 12345
        self._ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{RANDOM_PORT}/')

    def run(self):
        """Run the test."""
        tr = Test.AddTestRun(self.name)
        self._configure_traffic_server(tr)

        # The client process can be anything.
        tr.Processes.Default.Command = "echo"
        tr.Processes.Default.StartAfter(self._ts, ready=When.FileExists(self._ts.Disk.diags_log))

        # Override the default exclusion of error log.
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR:", "These tests should have error logs.")

        if self.enable_exit_on_load:
            self._ts.ReturnCode = 33
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "EMERGENCY: ", "Failure loading the certs results in an emergency error.")
            self._ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                "Traffic Server is fully initialized", "Traffic Server should exit upon the load failure.")
        else:
            self._ts.ReturnCode = 0
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "Traffic Server is fully initialized", "Traffic Server should start up successfully.")

        if self.is_testing_server_side:
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "ERROR:.*failed to load", "Verify that there is a cert loading issue.")
        else:
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "ERROR: failed to access cert", "Verify that there is a cert loading issue.")
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "Can't initialize the SSL client, HTTPS in remap rules will not function",
                "There should be an error loading the cert.")


# Test server cert loading.
Test_exit_on_cert_load_fail(
    "load server cert with exit on load disabled", is_testing_server_side=True, enable_exit_on_load=False).run()
Test_exit_on_cert_load_fail(
    "load server cert with exit on load enabled", is_testing_server_side=True, enable_exit_on_load=True).run()

# Test client cert loading.
Test_exit_on_cert_load_fail(
    "load client cert with exit on load disabled", is_testing_server_side=False, enable_exit_on_load=False).run()
Test_exit_on_cert_load_fail(
    "load client cert with exit on load enabled", is_testing_server_side=False, enable_exit_on_load=True).run()
