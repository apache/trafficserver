'''
Exercise the rate_limit SNI limiter's reject path against a TLS listener, so the
consumer-driven SSLNetVConnection teardown frees every rejected handshake VC
cleanly (no use-after-free or crash).
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


class RateLimitSniRejectTest:
    """Drive rate_limit's SNI reject path and assert ATS frees the VCs without a fault."""

    def __init__(self) -> None:
        tr = Test.AddTestRun('rate_limit SNI reject teardown')
        self._configure_trafficserver()
        self._configure_client(tr)

    def _configure_trafficserver(self) -> None:
        ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
        self._ts = ts
        ts.addDefaultSSLFiles()
        for line in ['ssl_multicert:', '  - dest_ip: "*"', '    ssl_cert_name: server.pem', '    ssl_key_name: server.key']:
            ts.Disk.ssl_multicert_yaml.AddLine(line)

        # One concurrent handshake for this SNI and no queue, so every further concurrent
        # handshake is rejected outright (TS_EVENT_ERROR) rather than queued. Named .config
        # (not .yaml) so autest treats it as a plain config file; the plugin parses it as
        # YAML regardless (YAML::LoadFile).
        ts.Disk.MakeConfigFile('rate_limit.config').AddLines([
            'selector:',
            '  - sni: rate.limited.com',
            '    limit: 1',
        ])
        ts.Disk.plugin_config.AddLine(f'rate_limit.so {ts.Variables.CONFIGDIR}/rate_limit.config')

        # Disable the freelist / ProxyAllocator so a freed SSLNetVConnection is really
        # free()'d rather than recycled; a stale-VC access then hits freed memory
        # instead of a still-valid recycled object.
        ts.Command += ' -f -F'

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'rate_limit',
            })

        # The reject disposition is reached...
        ts.Disk.traffic_out.Content = Testers.ContainsExpression('Rejecting connection', 'over-limit handshakes were rejected')
        # ...and ATS tears every rejected handshake VC down without a memory-safety fault.
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            'use-after-free|attempting free|SEGV|received signal', 'ATS must survive the reject churn')

    def _configure_client(self, tr) -> None:
        ts = self._ts
        client = os.path.join(Test.TestDirectory, 'rate_limit_sni_reject_client.sh')
        tr.Processes.Default.Command = f'bash {client} 127.0.0.1 {ts.Variables.ssl_port} rate.limited.com'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('rate_limit-reject-done', 'the client ran to completion')


RateLimitSniRejectTest()
