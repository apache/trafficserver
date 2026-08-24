'''
Existing cert/SNI reload tests reload while the server is idle. This one drives
continuous concurrent TLS handshakes and reloads ssl_multicert.yaml on top of
them, stressing the SSL/BIO ownership boundary of the layered TLS VConnection.
The swapped-in certificate must take effect, every handshake must succeed, and
ATS must not crash.
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
import sys

Test.Summary = __doc__


class TestTlsReloadUnderLoad:
    '''Reload TLS certs while concurrent handshakes are in flight; serving must not break.'''

    _ts_counter: int = 0

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        self._ts = self._configure_trafficserver()

    def _configure_trafficserver(self) -> 'Process':
        '''Configure Traffic Server with a swappable live certificate.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestTlsReloadUnderLoad._ts_counter}', enable_tls=True, enable_cache=False)
        TestTlsReloadUnderLoad._ts_counter += 1

        # signed-bar is the live cert; signed2-bar shares its key and is copied over
        # the live cert at reload time. Distinct fingerprints let the client prove the
        # swap took effect under load.
        ts.addSSLfile("ssl/signed-bar.pem")
        ts.addSSLfile("ssl/signed-bar.key")
        ts.addSSLfile("ssl/signed2-bar.pem")

        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: signed-bar.pem
    ssl_key_name: signed-bar.key
""".split("\n"))
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl',
            })

        # The reload must actually have run (otherwise the test would be vacuous).
        ts.Disk.diags_log.Content = Testers.ContainsExpression(
            "ssl_multicert.yaml finished loading", "the cert configuration must reload while load is in flight")
        # The reload-under-load must not crash or trip an assertion / sanitizer.
        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            "received signal|failed assertion", "ATS must not crash reloading certs under load")
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            "AddressSanitizer|use-after-free|runtime error:", "no memory-safety error reloading certs under load")
        return ts

    def run(self) -> None:
        '''Configure and run the TestRun.'''
        tr = Test.AddTestRun("reload certs while TLS handshakes are in flight")
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = (
            f'{sys.executable} {os.path.join(Test.TestDirectory, "tls_reload_under_load_client.py")} '
            f'-p {self._ts.Variables.ssl_port} --sni bar.com --ssldir {self._ts.Variables.SSLDir} '
            f'--live-cert signed-bar.pem '
            f'--v2-cert {os.path.join(self._ts.Variables.SSLDir, "signed2-bar.pem")} '
            f'--reloads 3 --duration 8 --concurrency 4')
        # traffic_ctl (invoked by the client to reload) needs the runroot environment.
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression("RESULT=PASS", "load + reload run must pass")
        tr.Processes.Default.Streams.All += Testers.ContainsExpression("FAILURES=0", "no handshake may fail across the reload")
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            "CERT_CHANGED=1", "the swapped-in certificate must be served after the reload")
        tr.StillRunningAfter = self._ts


TestTlsReloadUnderLoad().run()
