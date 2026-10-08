'''
Regression test for the rate_limit SNI limiter's queue accounting. A queued connection never
reserves a slot, so the sweep must reserve one before resuming it and a close must release
only a slot the connection owns. When the sweep resumes a queued connection while the limiter
is still full, the holder's close lands the active-slot counter on zero, the resumed
connection's close releases a slot it never held and wraps the counter below zero, and the
next reserve() trips a release assertion that aborts the server. ATS must survive the queue
churn without the counter wrapping.

The client drives resume-then-close rather than closing a connection while it is parked,
because ATS does not read the socket while the ClientHello hook is invoked: a FIN from a
parked client is invisible until the sweep reenables the VC. The close-while-queued branch
is out of scope for this test and cannot be reached from a client.
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


class RateLimitSniQueueTest:
    """Churn the rate_limit SNI queue and assert the active-slot counter never underflows."""

    def __init__(self) -> None:
        tr = Test.AddTestRun('rate_limit SNI queue accounting')
        self._configure_trafficserver()
        self._configure_client(tr)

    def _configure_trafficserver(self) -> None:
        ts = Test.MakeATSProcess('ts', enable_tls=True, enable_cache=False)
        self._ts = ts
        ts.addDefaultSSLFiles()
        for line in ['ssl_multicert:', '  - dest_ip: "*"', '    ssl_cert_name: server.pem', '    ssl_key_name: server.key']:
            ts.Disk.ssl_multicert_yaml.AddLine(line)

        # One concurrent handshake for this SNI and a queue that admits exactly one more.
        # No rate and no max_age -- the sweep's resume path alone drives the scenario, with
        # no rate-bucket or expiry timing to confound it. Named .config (not .yaml) so autest
        # treats it as a plain config file; the plugin parses it as YAML regardless.
        ts.Disk.MakeConfigFile('rate_limit.config').AddLines(
            [
                'selector:',
                '  - sni: rate.limited.com',
                '    limit: 1',
                '    queue:',
                '      size: 1',
            ])
        ts.Disk.plugin_config.AddLine(f'rate_limit.so {ts.Variables.CONFIGDIR}/rate_limit.config')

        # Disable the freelist / ProxyAllocator so freed objects are really released rather
        # than recycled, keeping allocation reuse from masking a stale access.
        ts.Command += ' -f -F'

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'rate_limit',
            })

        # The queue path is reached and the sweep resumes the queued connection once the
        # holder has released its slot...
        ts.Disk.traffic_out.Content = Testers.ContainsExpression('Queueing the VC', 'a connection was queued')
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            'Enabling queued VC', 'the sweep resumed the queued connection into a reserved slot')
        # ...nothing is turned away: with one holder and a queue of one, a rejection means the
        # holder never released and the probe hit a full queue, so the release path went
        # unexercised...
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            'Rejecting connection', 'no connection is rejected; the holder must release its slot')
        # ...and the active-slot counter never underflows. free() logs the counter after
        # dropping the lock, so a concurrent reserve() can legitimately make a release read back
        # as 1; only a wrapped counter is a defect, and an unmatched decrement of a uint32 at
        # limit 1 is unmistakable -- it reads 4294967295, never a small number.
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            r'Releasing a slot, active entities == [0-9]{4,}', 'a release must never wrap the counter')
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            '_active <= _limit|received signal', 'the active-slot counter must not underflow and abort ATS')

    def _configure_client(self, tr: 'TestRun') -> None:
        ts = self._ts
        client = os.path.join(Test.TestDirectory, 'rate_limit_sni_queue_client.sh')
        # The client paces itself on the plugin's debug lines in traffic.out, so it needs the
        # path rather than a guess at how long each step takes.
        tr.Processes.Default.Command = (
            f'bash {client} 127.0.0.1 {ts.Variables.ssl_port} rate.limited.com {ts.Disk.traffic_out.AbsPath}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            'rate_limit-queue-crash-done', 'the client ran to completion')


RateLimitSniQueueTest()
