'''
Regression test for the max_age expiry branch of the rate_limit SNI queue accounting. A
queued connection never reserves a slot, so when the sweep expires it the plugin must
detach it rather than release a slot it never held; otherwise the expiry underflows the
active-slot counter and the next reserve() trips a release assertion, aborting the server.
ATS must survive the expiry.
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

Test.Summary = __doc__

Test.SkipUnless(Condition.PluginExists('rate_limit.so'))


class RateLimitSniExpiryTest:
    """Age a queued connection out via max_age and assert the active-slot counter stays balanced."""

    def __init__(self) -> None:
        tr = Test.AddTestRun('rate_limit SNI queue max_age expiry')
        self._configure_trafficserver()
        self._configure_client(tr)

    def _configure_trafficserver(self) -> None:
        ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
        self._ts = ts
        ts.addDefaultSSLFiles()
        for line in ['ssl_multicert:', '  - dest_ip: "*"', '    ssl_cert_name: server.pem', '    ssl_key_name: server.key']:
            ts.Disk.ssl_multicert_yaml.AddLine(line)

        # One concurrent handshake for this SNI, a one-deep queue, and a 1s max age so the
        # sweep expires the queued connection. Named .config (not .yaml) so autest treats it
        # as a plain config file; the plugin parses it as YAML regardless.
        ts.Disk.MakeConfigFile('rate_limit.config').AddLines(
            [
                'selector:',
                '  - sni: rate.limited.com',
                '    limit: 1',
                '    queue:',
                '      size: 1',
                '      max_age: 1',
            ])
        ts.Disk.plugin_config.AddLine(f'rate_limit.so {ts.Variables.CONFIGDIR}/rate_limit.config')

        # Disable the freelist / ProxyAllocator so allocation behavior is not a confound; the
        # abort under test is a release-assertion, and this keeps the run representative of CI.
        ts.Command += ' -f -F'

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'rate_limit',
            })

        # The expiry branch is actually reached...
        ts.Disk.traffic_out.Content = Testers.ContainsExpression('too old', 'a queued connection was expired')
        # ...and expiring it does not underflow the active-slot counter into the release assertion.
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            '_active <= _limit|received signal', 'expiring a queued connection must not underflow and abort ATS')

    def _configure_client(self, tr) -> None:
        ts = self._ts
        client = os.path.join(Test.TestDirectory, 'rate_limit_sni_expiry_client.sh')
        tr.Processes.Default.Command = f'bash {client} 127.0.0.1 {ts.Variables.ssl_port} rate.limited.com'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('rate_limit-expiry-done', 'the client ran to completion')


RateLimitSniExpiryTest()
